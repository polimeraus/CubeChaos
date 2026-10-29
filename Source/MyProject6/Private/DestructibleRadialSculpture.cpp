#include "DestructibleRadialSculpture.h"
#include "SculptureMeshComponent.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMeshEditor.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "TimerManager.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

UClass* ADestructibleRadialSculpture::GetChunkComponentClass() const
{
    return USculptureMeshComponent::StaticClass();
}

namespace
{
    FDynamicMesh3 NewMesh()
    {
        FDynamicMesh3 Mesh;
        Mesh.EnableAttributes();
        Mesh.Attributes()->SetNumUVLayers(2);
        Mesh.Attributes()->EnableMaterialID();
        return Mesh;
    }

    // Faces have independent overlay normals: flat fins and caps, smooth sphere skin.
    void Triangle(FDynamicMesh3& Mesh, FVector A, FVector B, FVector C,
        const FVector& Outward, bool bSmooth = false, FVector SphereCenter = FVector::ZeroVector, double NormalSign = 1.)
    {
        FVector N = FVector::CrossProduct(B - A, C - A);
        if (N.SizeSquared() < 1.e-12) return; // Pole fans contain a repeated pole vertex.
        if (FVector::DotProduct(N, Outward) < 0) { Swap(B, C); N = -N; }
        N.Normalize();
        const FVector V[] = {A, B, C};
        int32 Vertices[3], Normals[3], UVs[3];
        for (int32 I = 0; I < 3; ++I)
        {
            Vertices[I] = Mesh.AppendVertex(V[I]);
            Normals[I] = Mesh.Attributes()->PrimaryNormals()->AppendElement(
                FVector3f(bSmooth ? (V[I] - SphereCenter).GetSafeNormal() * NormalSign : N));
            UVs[I] = Mesh.Attributes()->PrimaryUV()->AppendElement(FVector2f(V[I].X / 100., V[I].Z / 100.));
        }
        // Unreal renders clockwise front faces; keep the shading normals outward.
        const int32 T = Mesh.AppendTriangle(Vertices[0], Vertices[2], Vertices[1]);
        Mesh.Attributes()->PrimaryNormals()->SetTriangle(T, FIndex3i(Normals[0], Normals[2], Normals[1]));
        Mesh.Attributes()->PrimaryUV()->SetTriangle(T, FIndex3i(UVs[0], UVs[2], UVs[1]));
        Mesh.Attributes()->GetMaterialID()->SetValue(T, 0);
    }

    void Quad(FDynamicMesh3& Mesh, FVector A, FVector B, FVector C, FVector D, FVector Outward)
    {
        Triangle(Mesh, A, B, C, Outward);
        Triangle(Mesh, A, C, D, Outward);
    }

    // Eight corners: bottom ring then top ring. Each strip gets its own convex hull,
    // so the concave outer profile never acquires a solid collision shortcut.
    void BoxStrip(FDynamicMesh3& Mesh, FKAggregateGeom& Collision, const TArray<FVector>& V)
    {
        const int32 Faces[6][4] = {{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
        FVector Center = FVector::ZeroVector;
        for (const FVector& P : V) Center += P / 8.;
        for (const auto& F : Faces)
        {
            const FVector FaceCenter = (V[F[0]] + V[F[1]] + V[F[2]] + V[F[3]]) / 4.;
            Quad(Mesh, V[F[0]], V[F[1]], V[F[2]], V[F[3]], FaceCenter - Center);
        }
        FKConvexElem Hull;
        Hull.VertexData = V;
        Hull.UpdateElemBox();
        Collision.ConvexElems.Add(MoveTemp(Hull));
    }
}

ADestructibleRadialSculpture::ADestructibleRadialSculpture()
{
    // Keep the original native subobject name: placed actors serialize their
    // location/rotation/scale on this component. The shared chunk root sits below it.
    auto* SculptureRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SculptureRoot"));
    SetRootComponent(SculptureRoot);
    FloorRoot->SetupAttachment(SculptureRoot);
    SphereInterior = CreateDefaultSubobject<USceneComponent>(TEXT("SphereInterior"));
    SphereInterior->SetupAttachment(SculptureRoot);
    SetCanBeDamaged(true);
    FloorPattern = EChunkedFloorPattern::Solid;
    ChunkSideLength = 32;
    MaxGeneratedCells = 16000;
    bReactToMusic = false;
    bAutoPlayConeAnimation = false;
    ConeAnimationType = EConeStructureAnimationType::None;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> White(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    SculptureMaterial = White.Object;
}

void ADestructibleRadialSculpture::OnConstruction(const FTransform& Transform)
{
    // The old implementation used transient per-piece rigid bodies. Clean up those
    // components as well when loading or recompiling an existing placed Blueprint.
    TInlineComponentArray<UDynamicMeshComponent*> Owned(this);
    for (auto* C : Owned)
        if (IsValid(C) && C->CreationMethod == EComponentCreationMethod::UserConstructionScript)
            C->DestroyComponent();
    Super::OnConstruction(Transform);
}

FVector ADestructibleRadialSculpture::GetSphereLocalCenter() const
{
    const float R = FMath::Clamp(SphereRadius, 10.f, 1000.f);
    const float Neck = FMath::Clamp(NeckRadius, 5.f, FMath::Min(R * .9f, FMath::Clamp(OuterRadius, 50.f, 3000.f) * .8f));
    return FVector(0, 0, (bIncludeBase ? FMath::Clamp(BaseThickness, 2.f, 100.f) : 0.f)
        + FMath::Clamp(FinHeight, 50.f, 3000.f) + FMath::Sqrt(R * R - Neck * Neck));
}

void ADestructibleRadialSculpture::RebuildSculpture()
{
    if (!IsTemplate() && GetWorld()) { SetLifeSpan(0); GenerateFloor(false); }
}

void ADestructibleRadialSculpture::SetSculptureMaterial(UMaterialInterface* Material)
{
    SculptureMaterial = Material;
    UpdateSculptureMaterials();
    if (!IsTemplate()) RefreshFloorMaterials();
}

void ADestructibleRadialSculpture::SetGlowAnimationType(ERadialSculptureGlowAnimation Animation)
{
    GlowAnimation = Animation;
    RefreshSculptureGlow();
}

void ADestructibleRadialSculpture::RefreshSculptureGlow()
{
    UpdateSculptureMaterials();
    if (!IsTemplate()) RefreshFloorMaterials();
}

void ADestructibleRadialSculpture::UpdateSculptureMaterials()
{
    if (!IsTemplate() && GlowAnimation != ERadialSculptureGlowAnimation::None)
    {
        UMaterialInterface* Parent = SculptureGlowMaterial.LoadSynchronous();
        if (Parent && (!SculptureGlowInstance || SculptureGlowInstance->Parent != Parent))
            SculptureGlowInstance = UMaterialInstanceDynamic::Create(Parent, this);
        if (!Parent) SculptureGlowInstance = nullptr;
    }
    else SculptureGlowInstance = nullptr;
    if (SculptureGlowInstance)
    {
        SculptureGlowInstance->SetScalarParameterValue(TEXT("Mode"), float(GlowAnimation));
        SculptureGlowInstance->SetScalarParameterValue(TEXT("Duration"), FMath::Max(GlowDuration,.1f));
        SculptureGlowInstance->SetScalarParameterValue(TEXT("Pause"), FMath::Max(GlowLoopDelay,0.f));
        SculptureGlowInstance->SetScalarParameterValue(TEXT("FinCount"), GeneratedFinCount);
        SculptureGlowInstance->SetScalarParameterValue(TEXT("LegWidth"), FMath::Clamp(GlowLegWidth,1.f,8.f));
        SculptureGlowInstance->SetScalarParameterValue(TEXT("BandHeight"), FMath::Max(GlowBandHeight,1.f));
        SculptureGlowInstance->SetScalarParameterValue(TEXT("Height"), GeneratedGlowHeight);
        SculptureGlowInstance->SetScalarParameterValue(TEXT("Intensity"), FMath::Max(SculptureGlowIntensity,0.f));
        SculptureGlowInstance->SetScalarParameterValue(TEXT("PreviewTime"), GlowPreviewTime);
        SculptureGlowInstance->SetVectorParameterValue(TEXT("Tint"), SculptureGlowColor);
    }
    FloorMaterials = {SculptureMaterial ? SculptureMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface)};
    if (SculptureGlowInstance) FloorMaterials.Add(SculptureGlowInstance);
}

FVector ADestructibleRadialSculpture::GetSphereWorldCenter() const
{ return SphereInterior->GetComponentLocation(); }

float ADestructibleRadialSculpture::GetSphereInnerRadius() const
{
    const float R = FMath::Clamp(SphereRadius, 10.f, 1000.f);
    return R - FMath::Clamp(SphereWallThickness, 1.f, R * .9f);
}

float ADestructibleRadialSculpture::GetSphereInteriorWorldRadius() const
{ return InteriorClearanceRadius * GetActorScale3D().GetAbs().GetMin(); }

bool ADestructibleRadialSculpture::TryGetSphereSpawnTransform(FTransform& OutTransform, float ActorRadius,
    FVector LocalOffset, FRotator LocalRotation, TEnumAsByte<ECollisionChannel> CollisionChannel) const
{
    OutTransform = FTransform::Identity;
    const double MinScale = GetActorScale3D().GetAbs().GetMin();
    if (!GetWorld() || !FMath::IsFinite(ActorRadius) || ActorRadius <= 0.f || MinScale < UE_SMALL_NUMBER
        || LocalOffset.ContainsNaN() || LocalRotation.ContainsNaN()
        || LocalOffset.Length() + ActorRadius / MinScale > InteriorClearanceRadius) return false;
    const FTransform Frame = SphereInterior->GetComponentTransform();
    const FVector Position = Frame.TransformPosition(LocalOffset);
    const FQuat Rotation = Frame.GetRotation() * LocalRotation.Quaternion();
    // Include the sculpture and previously spawned actors in the overlap query.
    if (GetWorld()->OverlapBlockingTestByChannel(Position, Rotation, CollisionChannel,
        FCollisionShape::MakeSphere(ActorRadius), FCollisionQueryParams(SCENE_QUERY_STAT(SculptureInterior)))) return false;
    OutTransform = FTransform(Rotation, Position, FVector::OneVector);
    return true;
}

void ADestructibleRadialSculpture::AddPiece(FPiece&& Piece)
{
    if (bBudgetExceeded || Piece.Mesh.TriangleCount() == 0) return;
    if (Pieces.Num() >= FMath::Clamp(MaxGeneratedCells, 1, 16000)) { bBudgetExceeded = true; return; }
    for (int32 V : Piece.Mesh.VertexIndicesItr()) Piece.Bounds += FVector(Piece.Mesh.GetVertex(V));
    // UV0 remains available to arbitrary surface materials. UV1 carries exact leg
    // identity (not angle near the center) and continuous local height for the glow.
    auto* GlowUV = Piece.Mesh.Attributes()->GetUVLayer(1);
    for (int32 T : Piece.Mesh.TriangleIndicesItr())
    {
        const auto V = Piece.Mesh.GetTriangle(T);
        FIndex3i UV;
        for (int32 I=0; I<3; ++I)
            UV[I] = GlowUV->AppendElement(FVector2f(float(Piece.FinIndex), float(Piece.Mesh.GetVertex(V[I]).Z)));
        GlowUV->SetTriangle(T, UV);
    }
    if (Piece.Part != EPart::Sphere)
    {
        constexpr int32 Faces[6][4] = {{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
        for (const FKConvexElem& Hull : Piece.Collision.ConvexElems)
        {
            FVector Center = FVector::ZeroVector;
            for (const FVector& V : Hull.VertexData) Center += V / 8.;
            auto& Planes = Piece.HullPlanes.Emplace_GetRef();
            for (const auto& F : Faces)
            {
                const FVector A = Hull.VertexData[F[0]], B = Hull.VertexData[F[1]], C = Hull.VertexData[F[2]], D = Hull.VertexData[F[3]];
                FVector N = FVector::CrossProduct(B-A,C-A);
                if (!N.Normalize()) { N = FVector::CrossProduct(C-A,D-A); if (!N.Normalize()) continue; }
                if (FVector::DotProduct(N, (A+B+C+D)*.25-Center) < 0) N = -N;
                Planes.Add(FPlane(A,N));
            }
        }
    }
    Pieces.Add(MoveTemp(Piece));
}

bool ADestructibleRadialSculpture::CalculateLayout()
{
    TArray<FPiece> Previous = MoveTemp(Pieces);
    Pieces.Reset(); bBudgetExceeded = false; GenerationWarning.Reset();
    const double Size = FMath::Clamp(PieceSize, 20.f, 200.f);
    const int32 Fins = FMath::Clamp(FinCount, 4, 32);
    const double Radius = FMath::Clamp(OuterRadius, 50.f, 3000.f);
    const double Height = FMath::Clamp(FinHeight, 50.f, 3000.f);
    const double R = FMath::Clamp(SphereRadius, 10.f, 1000.f);
    const double Wall = FMath::Clamp(double(SphereWallThickness), 1., R * .9);
    const double InnerSphereRadius = R - Wall;
    const FVector SphereCenter = GetSphereLocalCenter();
    double ClearanceRadius = InnerSphereRadius;
    const double Neck = FMath::Clamp(double(NeckRadius), 5., FMath::Min(R * .9, Radius * .8));
    const double HalfThickness = FMath::Clamp(FinThickness, 1.f, 30.f) * .5;
    const double BaseZ = bIncludeBase ? FMath::Clamp(BaseThickness, 2.f, 100.f) : 0.;
    const double Inner = FMath::Min(3., Neck * .1);
    const double Exponent = FMath::Clamp(CurveExponent, 1.f, 4.f);
    auto Profile = [&](double T) { return Neck + (Radius-Neck)*FMath::Pow(FMath::Max(0.,1.-T),Exponent); };
    const int32 Rows = FMath::CeilToInt(Height/Size);
    constexpr int32 CurveSamples = 3;
    for (int32 Fin=0; Fin<Fins && !bBudgetExceeded; ++Fin)
    {
        const double Angle=2.*PI*Fin/Fins;
        const FVector Radial(FMath::Cos(Angle),FMath::Sin(Angle),0), Side(-Radial.Y,Radial.X,0);
        auto P=[&](double X,double Y,double Z)
        {
            // End supports at the lower outer skin instead of letting their tips enter the cavity.
            if (X < Neck) Z = FMath::Min(Z, SphereCenter.Z - FMath::Sqrt(R*R-X*X));
            return Radial*X+Side*Y+FVector(0,0,Z);
        };
        for (int32 Row=0; Row<Rows && !bBudgetExceeded; ++Row)
        {
            const double T0=double(Row)/Rows, T1=double(Row+1)/Rows;
            const int32 Columns=FMath::CeilToInt((Profile(T0)-Inner)/Size);
            // Share every horizontal cut across adjacent columns. Include the exact
            // heights where the curve crosses grid lines, so clipped neighbors meet
            // without tiny cracks or stepped outer edges.
            TArray<double> Cuts={T0,T1};
            for (int32 S=1;S<CurveSamples;++S) Cuts.Add(FMath::Lerp(T0,T1,double(S)/CurveSamples));
            for (int32 Col=1;Col<Columns;++Col)
            {
                const double X=Inner+Size*Col;
                if (X<=Neck || X>=Radius) continue;
                const double Cut=1.-FMath::Pow((X-Neck)/(Radius-Neck),1./Exponent);
                if (Cut>T0+1.e-6 && Cut<T1-1.e-6) Cuts.AddUnique(Cut);
            }
            Cuts.Sort();
            for (int32 Col=0; Col<Columns && !bBudgetExceeded; ++Col)
            {
                const double X0=Inner+Size*Col, X1=X0+Size;
                const double Cut=X0>Neck ? 1.-FMath::Pow((X0-Neck)/(Radius-Neck),1./Exponent) : 1.;
                const double End=FMath::Min(T1,Cut);
                if (End-T0<1.e-6) continue;
                FPiece Piece; Piece.Mesh=NewMesh(); Piece.FinIndex=Fin;
                for (int32 S=0; S<Cuts.Num()-1; ++S)
                {
                    const double A=Cuts[S],B=Cuts[S+1];
                    if (A>=End-1.e-6 || Profile((A+B)*.5)<=X0+1.e-6) continue;
                    const double XA=FMath::Max(X0,FMath::Min(X1,Profile(A))), XB=FMath::Max(X0,FMath::Min(X1,Profile(B)));
                    const double Z0=BaseZ+Height*A,Z1=BaseZ+Height*B;
                    const double Width = X0 < Neck && Z1 > SphereCenter.Z-R ? 2. : Size;
                    for (double X=X0;X<FMath::Max(XA,XB)-1.e-6;X+=Width)
                    {
                        const double A0=FMath::Min(X,XA), A1=FMath::Min(X+Width,XA);
                        const double B0=FMath::Min(X,XB), B1=FMath::Min(X+Width,XB);
                        TArray<FVector> Corners={
                            P(A0,-HalfThickness,Z0),P(A1,-HalfThickness,Z0),P(A1,HalfThickness,Z0),P(A0,HalfThickness,Z0),
                            P(B0,-HalfThickness,Z1),P(B1,-HalfThickness,Z1),P(B1,HalfThickness,Z1),P(B0,HalfThickness,Z1)};
                        if (FMath::Max(Corners[4].Z-Corners[0].Z,Corners[5].Z-Corners[1].Z)>1.e-6)
                            BoxStrip(Piece.Mesh,Piece.Collision,Corners);
                    }
                }
                AddPiece(MoveTemp(Piece));
            }
        }
    }
    // Only the wall is subdivided: the cavity has neither mesh nor collision.
    const FVector Faces[]={FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1)};
    const int32 Sides=FMath::Max(2,FMath::CeilToInt(2*R/Size)), ShellCount=FMath::CeilToInt(Wall/Size);
    constexpr int32 Samples=3;
    for (const FVector& Face:Faces)
    {
        const FVector U=FMath::Abs(Face.Z)>.5 ? FVector::XAxisVector : FVector::ZAxisVector;
        const FVector V=FVector::CrossProduct(Face,U);
        for (int32 Layer=0;Layer<ShellCount && !bBudgetExceeded;++Layer)
        for (int32 Y=0;Y<Sides && !bBudgetExceeded;++Y)
        for (int32 X=0;X<Sides && !bBudgetExceeded;++X)
        {
            FPiece Piece;Piece.Part=EPart::Sphere;Piece.Mesh=NewMesh();Piece.SphereCenter=SphereCenter;
            Piece.FaceNormal=Face;Piece.AxisU=U;Piece.AxisV=V;
            Piece.InnerRadius=InnerSphereRadius+Wall*Layer/ShellCount;
            Piece.OuterRadius=InnerSphereRadius+Wall*(Layer+1)/ShellCount;
            Piece.U0=-1.+2.*X/Sides;Piece.U1=-1.+2.*(X+1)/Sides;
            Piece.V0=-1.+2.*Y/Sides;Piece.V1=-1.+2.*(Y+1)/Sides;
            auto P=[&](double Distance,int32 A,int32 B)
            { return SphereCenter+(Face+U*FMath::Lerp(Piece.U0,Piece.U1,double(A)/Samples)+
                V*FMath::Lerp(Piece.V0,Piece.V1,double(B)/Samples)).GetSafeNormal()*Distance; };
            const double Lo=Piece.InnerRadius, Hi=Piece.OuterRadius;
            const FVector Interior=SphereCenter+(Face+U*(Piece.U0+Piece.U1)*.5+V*(Piece.V0+Piece.V1)*.5).GetSafeNormal()*(Lo+Hi)*.5;
            for (int32 A=0;A<Samples;++A)
            for (int32 B=0;B<Samples;++B)
            {
                // Small hulls follow the concave inner skin; a hull spanning the whole
                // cell would fill part of the cavity behind the visible inner surface.
                FKConvexElem Hull;
                Hull.VertexData={P(Lo,A,B),P(Lo,A+1,B),P(Lo,A+1,B+1),P(Lo,A,B+1),
                    P(Hi,A,B),P(Hi,A+1,B),P(Hi,A+1,B+1),P(Hi,A,B+1)};
                Hull.UpdateElemBox();Piece.Collision.ConvexElems.Add(MoveTemp(Hull));
                for (int32 Outer=0;Outer<2;++Outer)
                {
                    const double Distance=Outer?Hi:Lo;
                    const FVector P0=P(Distance,A,B),P1=P(Distance,A+1,B),P2=P(Distance,A+1,B+1),P3=P(Distance,A,B+1);
                    const FVector N=(P0+P1+P2+P3)*.25-SphereCenter;
                    Triangle(Piece.Mesh,P0,P1,P2,Outer?N:-N,true,SphereCenter,Outer?1.:-1.);
                    Triangle(Piece.Mesh,P0,P2,P3,Outer?N:-N,true,SphereCenter,Outer?1.:-1.);
                    if (!Outer && Layer==0)
                    {
                        ClearanceRadius=FMath::Min(ClearanceRadius,FMath::Abs(FVector::DotProduct(P0-SphereCenter,
                            FVector::CrossProduct(P1-P0,P2-P0).GetSafeNormal())));
                        ClearanceRadius=FMath::Min(ClearanceRadius,FMath::Abs(FVector::DotProduct(P0-SphereCenter,
                            FVector::CrossProduct(P2-P0,P3-P0).GetSafeNormal())));
                    }
                }
            }
            auto Cap=[&](FVector A,FVector B,FVector C,FVector D)
            { Quad(Piece.Mesh,A,B,C,D,(A+B+C+D)*.25-Interior); };
            for (int32 I=0;I<Samples;++I)
            {
                Cap(P(Lo,0,I),P(Hi,0,I),P(Hi,0,I+1),P(Lo,0,I+1));
                Cap(P(Lo,Samples,I),P(Hi,Samples,I),P(Hi,Samples,I+1),P(Lo,Samples,I+1));
                Cap(P(Lo,I,0),P(Hi,I,0),P(Hi,I+1,0),P(Lo,I+1,0));
                Cap(P(Lo,I,Samples),P(Hi,I,Samples),P(Hi,I+1,Samples),P(Lo,I+1,Samples));
            }
            AddPiece(MoveTemp(Piece));
        }
    }
    const double Extent=Radius+FMath::Clamp(BaseMargin,0.f,500.f);
    if (bIncludeBase)
    {
        const int32 Tiles=FMath::CeilToInt(2*Extent/Size), Depth=FMath::CeilToInt(BaseZ/Size);
        const double Step=2*Extent/Tiles;
        for (int32 X=0;X<Tiles && !bBudgetExceeded;++X)
        for (int32 Y=0;Y<Tiles && !bBudgetExceeded;++Y)
        for (int32 Z=0;Z<Depth && !bBudgetExceeded;++Z)
        {
            const double X0=-Extent+Step*X,X1=X0+Step,Y0=-Extent+Step*Y,Y1=Y0+Step;
            const double Z0=BaseZ*Z/Depth,Z1=BaseZ*(Z+1)/Depth;
            FPiece Piece;Piece.Part=EPart::Base;Piece.Mesh=NewMesh();
            BoxStrip(Piece.Mesh,Piece.Collision,{FVector(X0,Y0,Z0),FVector(X1,Y0,Z0),FVector(X1,Y1,Z0),FVector(X0,Y1,Z0),
                FVector(X0,Y0,Z1),FVector(X1,Y0,Z1),FVector(X1,Y1,Z1),FVector(X0,Y1,Z1)});
            AddPiece(MoveTemp(Piece));
        }
    }
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce sculpture dimensions. Previous geometry retained.");
        return false;
    }
    SetLifeSpan(0);
    InteriorClearanceRadius=FMath::Max(0.,ClearanceRadius-.5);
    SphereInterior->SetRelativeLocation(SphereCenter);
    GeneratedPieceCount=RemainingPieceCount=Pieces.Num();
    GeneratedFinCount=Fins;
    GeneratedGlowHeight=SphereCenter.Z+R;
    UpdateSculptureMaterials();
    DefaultCellHealth=FMath::Max(PieceHealth,1.f);
    CellCount=FIntPoint(Pieces.Num(),1);
    ChunkCount=FIntPoint(FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1);
    EffectiveCellDimensions=CellDimensions=FVector(Size);
    EffectiveGridSize=AreaSize=FVector2D(2*Extent);GridLocalMin=-EffectiveGridSize*.5;CellSpacing=FVector2D::ZeroVector;
    return true;
}

bool ADestructibleRadialSculpture::FPiece::Contains(FVector Position,double Tolerance) const
{
    if (!Bounds.ExpandBy(Tolerance).IsInsideOrOn(Position)) return false;
    if (Part==EPart::Sphere)
    {
        const FVector D=Position-SphereCenter;
        const double Radius=D.Length(),W=FVector::DotProduct(D,FaceNormal);
        if (Radius<InnerRadius-Tolerance || Radius>OuterRadius+Tolerance || W < -Tolerance) return false;
        if (Radius<Tolerance) return InnerRadius==0;
        const double SafeW=FMath::Max(W,1.e-8), U=FVector::DotProduct(D,AxisU)/SafeW,V=FVector::DotProduct(D,AxisV)/SafeW;
        const double Epsilon=Tolerance/SafeW;
        return U>=U0-Epsilon && U<=U1+Epsilon && V>=V0-Epsilon && V<=V1+Epsilon;
    }
    for (const auto& Planes:HullPlanes)
    {
        bool Inside=true;
        for (const FPlane& Plane:Planes) if (Plane.PlaneDot(Position)>Tolerance) { Inside=false;break; }
        if (Inside) return true;
    }
    return false;
}

bool ADestructibleRadialSculpture::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector ADestructibleRadialSculpture::GetCellLocalCenter(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Bounds.GetCenter():FVector::ZeroVector; }
FTransform ADestructibleRadialSculpture::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell)?FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)):FTransform::Identity;
}
void ADestructibleRadialSculpture::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh=NewMesh();FDynamicMeshEditor Editor(&Mesh);
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128),End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I) if (Cells.IsValidIndex(I) && Cells[I].bExists)
    {
        FMeshIndexMappings Map;Editor.AppendMesh(&Pieces[I].Mesh,Map);
        if (SculptureGlowInstance)
        {
            // A separate additive skin retains the chosen surface material and
            // disappears with each destroyed cell. It adds no collision bodies.
            FDynamicMesh3 Glow(Pieces[I].Mesh);
            const auto* Normals=Glow.Attributes()->PrimaryNormals();
            for (int32 T:Glow.TriangleIndicesItr())
            {
                const auto V=Glow.GetTriangle(T), N=Normals->GetTriangle(T);
                for (int32 K=0;K<3;++K)
                    Glow.SetVertex(V[K],Glow.GetVertex(V[K])+FVector3d(Normals->GetElement(N[K]))*.05);
                Glow.Attributes()->GetMaterialID()->SetValue(T,1);
            }
            FMeshIndexMappings GlowMap;Editor.AppendMesh(&Glow,GlowMap);
        }
    }
}
void ADestructibleRadialSculpture::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
{
    Collision.EmptyElements();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128),End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    if (bGenerateCollision) for (int32 I=First;I<End;++I) if (Cells.IsValidIndex(I) && Cells[I].bExists)
        Collision.ConvexElems.Append(Pieces[I].Collision.ConvexElems);
}
bool ADestructibleRadialSculpture::FindPiece(FVector Position,FIntPoint& Cell,double Tolerance,int32 ChunkIndex) const
{
    Cell=FIntPoint::NoneValue;double Best=TNumericLimits<double>::Max();
    const int32 First=ChunkIndex==INDEX_NONE?0:ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=ChunkIndex==INDEX_NONE?Pieces.Num():FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I) if (Cells.IsValidIndex(I) && Cells[I].bExists && Pieces[I].Contains(Position,Tolerance))
    {
        const double Distance=FVector::DistSquared(Position,Pieces[I].Bounds.GetCenter());
        if (Distance<Best) { Best=Distance;Cell=FIntPoint(I,0); }
    }
    return Cell!=FIntPoint::NoneValue;
}
bool ADestructibleRadialSculpture::WorldLocationToCell(FVector Position,FIntPoint& Cell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(Position),Cell,.05); }
bool ADestructibleRadialSculpture::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    if (!CanBeDamaged()) return false;
    const FVector P=GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
    const int32 Chunk=ChunkComponents.IndexOfByPredicate([&](const auto& C){return C==Hit.GetComponent();});
    return FindPiece(P,Cell,.5,Chunk);
}
int32 ADestructibleRadialSculpture::SelectCellsInRadius(FVector Origin,float Radius,EChunkedFloorBlastShape Shape,
    TFunctionRef<void(int32,float)> Function)
{
    if (Radius<=0 || !CanBeDamaged()) return 0;
    const FVector Local=GetActorTransform().InverseTransformPosition(Origin),Scale=GetActorScale3D().GetAbs();
    int32 Count=0;
    for (int32 I=0;I<Pieces.Num();++I) if (Cells.IsValidIndex(I) && Cells[I].bExists)
    {
        const FVector Point = Shape == EChunkedFloorBlastShape::Sphere
            ? Pieces[I].Bounds.GetClosestPointTo(Local) : Pieces[I].Bounds.GetCenter();
        const FVector D=((Point-Local)*Scale).GetAbs();
        const double Distance=Shape==EChunkedFloorBlastShape::Square?D.GetMax():
            (Shape==EChunkedFloorBlastShape::Diamond?D.X+D.Y+D.Z:D.Length());
        if (Distance<=Radius) { Function(I,float(Distance));++Count; }
    }
    return Count;
}
void ADestructibleRadialSculpture::MarkCellChunksDirty(FIntPoint Cell)
{ if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128)); }
void ADestructibleRadialSculpture::FinalizeDestructionBatch(double CurrentTime)
{
    RemainingPieceCount=0;
    for (const auto& Cell:Cells) RemainingPieceCount+=Cell.bExists?1:0;
    if (RemainingPieceCount==0 && GetWorld() && GetWorld()->IsGameWorld()) SetLifeSpan(FMath::Max(.1f,FlashDuration+.05f));
}

float ADestructibleRadialSculpture::TakeDamage(float DamageAmount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{
    if (!CanBeDamaged() || DamageAmount<=0 || !FMath::IsFinite(DamageAmount) || RemainingPieceCount==0) return 0;
    bool Applied=false;
    if (Event.IsOfType(FPointDamageEvent::ClassID))
        Applied=DamageCellFromHit(static_cast<const FPointDamageEvent&>(Event).HitInfo,DamageAmount);
    else if (Event.IsOfType(FRadialDamageEvent::ClassID))
    {
        const auto& Radial=static_cast<const FRadialDamageEvent&>(Event);
        SelectCellsInRadius(Radial.Origin,Radial.Params.GetMaxRadius(),EChunkedFloorBlastShape::Circle,
            [&](int32 I,float Distance)
            {
                const auto* Chunk=ChunkComponents[I/FMath::Clamp(ChunkSideLength,4,128)].Get();
                if (!Radial.ComponentHits.IsEmpty() && !Radial.ComponentHits.ContainsByPredicate([&](const FHitResult& H){return H.GetComponent()==Chunk;})) return;
                const float Damage=FMath::Lerp(FMath::Clamp(Radial.Params.MinimumDamage,0.f,DamageAmount),DamageAmount,Radial.Params.GetDamageScale(Distance));
                if (Damage<=0 || !Cells[I].bDestructible || PendingCellIndices.Contains(I)) return;
                Cells[I].Health-=Damage;Applied=true;
                if (Cells[I].Health<=0) QueueCellDestruction(I,bUseDestructionRipple?Distance/FMath::Max(DestructionRippleSpeed,1.f):0,true);
            });
        ProcessQueuedDestructions(GetWorld()->GetTimeSeconds());
    }
    else
    {
        for (int32 I=0;I<Cells.Num();++I) if (Cells[I].bExists && Cells[I].bDestructible && !PendingCellIndices.Contains(I))
        { Cells[I].Health-=DamageAmount;Applied=true;if (Cells[I].Health<=0) QueueCellDestruction(I,0,false); }
        ProcessQueuedDestructions(GetWorld()->GetTimeSeconds());
    }
    if (Applied) Super::TakeDamage(DamageAmount,Event,EventInstigator,Causer);
    return Applied?DamageAmount:0;
}
int32 ADestructibleRadialSculpture::ApplyExplosion(FVector Origin,float Radius,float Damage)
{
    if (!CanBeDamaged() || !FMath::IsFinite(Radius) || !FMath::IsFinite(Damage) || Radius<=0 || Damage<=0) return 0;
    return ApplyExplosionAtWorldLocation(Origin,Radius,Damage);
}
void ADestructibleRadialSculpture::ShatterSculpture(FVector WorldImpulseDirection)
{
    if (!CanBeDamaged()) return;
    for (int32 I=0;I<Cells.Num();++I) QueueCellDestruction(I,0,false);
    ProcessQueuedDestructions(GetWorld()?GetWorld()->GetTimeSeconds():0);
}
