#if WITH_DEV_AUTOMATION_TESTS
#include "ChunkedDestructibleSpiralTower.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpiralTowerTest,"MyProject6.Destructible.SpiralTower",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSpiralTowerTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* A=World->SpawnActor<AChunkedDestructibleSpiralTower>();
    if (!TestNotNull(TEXT("Native tower spawns"),A)) return false;
    A->FlashDuration=0; A->bPlayDestroyTickSound=false;
    TestFalse(TEXT("Square foundation is disabled by default"),A->bFoundation);
    auto* Pad=Cast<ALaunchPad>(A->GroundLaunchPad->GetChildActor());
    if (!TestNotNull(TEXT("Center launch pad exists by default"),Pad)) return false;
    TestTrue(TEXT("Pad automatically clears the summit"),Pad->bUseTargetHeight && Pad->TargetRiseHeight>A->GeneratedHeight);
    TestTrue(TEXT("Pad's diagonal fits within the entire shaft"),Pad->Size*FMath::Sqrt(2.f)<A->ClearShaftDiameter);
    TestTrue(TEXT("Pad launches vertically from ground level"),Pad->GetActorLocation().IsNearlyZero() && Pad->ForwardSpeed==0 && Pad->bCenterBeforeLaunch);
    A->Traversal.bGroundLaunchPad=false;A->RebuildTower();
    TestNull(TEXT("Disabling pad removes its actor and collision"),A->GroundLaunchPad->GetChildActor());
    A->Traversal.bGroundLaunchPad=true;A->RebuildTower();
    TestNotNull(TEXT("Re-enabling pad recreates it"),A->GroundLaunchPad->GetChildActor());
    if (!TestNotNull(TEXT("Default flowing glow material is assigned"),A->GlowMaterialInstance.Get())) return false;
    const int32 WithoutFoundation=A->GeneratedPieceCount;
    A->bFoundation=true;A->RebuildTower();
    TestTrue(TEXT("Optional foundation adds masonry"),A->GeneratedPieceCount>WithoutFoundation);
    A->GlowBandHeight=650;A->GlowTrailLength=2100;A->GlowSpeed=275;A->GlowPreviewTime=2;A->RefreshGlowAnimation();
    TestEqual(TEXT("Wave coverage is configurable"),A->GlowMaterialInstance->K2_GetScalarParameterValue(TEXT("BandHeight")),650.f);
    TestEqual(TEXT("Trail length is independent of vertical coverage"),A->GlowMaterialInstance->K2_GetScalarParameterValue(TEXT("TrailLength")),2100.f);
    TestEqual(TEXT("Wave travels the generated staircase length"),A->GlowMaterialInstance->K2_GetScalarParameterValue(TEXT("PathLength")),A->GeneratedGlowPathLength);
    TestEqual(TEXT("Wave speed is configurable"),A->GlowMaterialInstance->K2_GetScalarParameterValue(TEXT("Speed")),275.f);
    A->bFlowingGlow=false;A->RefreshGlowAnimation();
    TestEqual(TEXT("Disabling glow fully returns to the base materials"),A->GlowMaterialInstance->K2_GetScalarParameterValue(TEXT("Brightness")),0.f);
    A->bFlowingGlow=true;A->RefreshGlowAnimation();
    auto Triangles=[&]() { int32 N=0; for (auto& C:A->ChunkComponents) N+=C->GetDynamicMesh()->GetTriangleCount(); return N; };
    auto Trace=[&](FVector S,FVector E) { return World->LineTraceTestByChannel(S,E,ECC_Visibility); };
    const int32 Count=A->GeneratedPieceCount,Original=Triangles();
    TestTrue(TEXT("Reference spiral fits the fragment budget"),Count>1000 && Count<30000 && A->GenerationWarning.IsEmpty());
    TestEqual(TEXT("Stairs reach the summit landing below the crescent"),A->GeneratedStepCount,500);
    TestTrue(TEXT("Wall bands become shorter toward the summit"),A->WallHeightAtTurn(A->Turns)<A->WallHeightAtTurn(0)*.6);
    int32 GlowingPieces=0;bool GlowingWall=false,GlowingStair=false;
    for (const auto& P:A->Pieces) if (P.bReceivesGlow)
        { ++GlowingPieces;GlowingWall|=P.Material==0;GlowingStair|=P.Material==1; }
    TestTrue(TEXT("Both walls and stairs participate in the wave"),GlowingWall && GlowingStair);
    TestEqual(TEXT("Foundation receives no glow skin"),GlowingPieces,WithoutFoundation);
    TestEqual(TEXT("All fragments retain base faces plus eligible additive skins"),Original,12*(Count+GlowingPieces));
    auto VerifyStairFlow=[&]()
    {
        TMap<float,double> Treads;
        double PreviousDistance=-1;
        for (const auto& P:A->Pieces) if (P.Material==1 && P.bReceivesGlow)
        {
            const double Distance=FMath::Min(P.GlowUV[0].X,P.GlowUV[2].X);
            if (!TestTrue(TEXT("Glow progresses continuously along stairs across whole-turn seams"),Distance>=PreviousDistance)) return false;
            PreviousDistance=Distance;
            if (!TestTrue(TEXT("Each stair's upper surface lies on the bright path centerline"),FMath::Abs(P.GlowUV[4].Y)<.001)) return false;
            Treads.Add(P.GlowUV[0].X,P.V[4].Z);
        }
        bool FoundTallWall=false;float LastGlowDistance=0;
        for (const auto& P:A->Pieces) if (P.bReceivesGlow)
        {
            for (const FVector2f& UV:P.GlowUV) LastGlowDistance=FMath::Max(LastGlowDistance,UV.X);
            for (int32 I=0;I<4;++I)
            {
                if (!TestTrue(TEXT("Glow extends vertically on walls without advancing its path position"),P.GlowUV[I].X==P.GlowUV[I+4].X)) return false;
                if (!TestTrue(TEXT("Wall vertical spread is measured relative to its stair"),
                    FMath::Abs((P.GlowUV[I+4].Y-P.GlowUV[I].Y)-(P.V[I+4].Z-P.V[I].Z))<.001)) return false;
            }
            if (P.Material==0)
            {
                const double* TreadZ=Treads.Find(P.GlowUV[0].X);
                if (TreadZ && !TestTrue(TEXT("Wall and adjacent stair share the same glow origin"),
                    FMath::Abs(P.V[0].Z-P.GlowUV[0].Y-*TreadZ)<.001)) return false;
                FoundTallWall|=P.Bounds.GetSize().Z>80;
            }
        }
        if (!TestTrue(TEXT("Multiple turns have an unwrapped path much longer than the tower height"),
            A->GeneratedGlowPathLength>A->GeneratedHeight*3 && PreviousDistance>0)) return false;
        if (!TestEqual(TEXT("Cycle includes the final crown wall after the last stair"),LastGlowDistance,A->GeneratedGlowPathLength)) return false;
        if (!TestTrue(TEXT("Glow includes full-height wall columns"),FoundTallWall)) return false;
        // Probe the real entrance geometry, not just metadata: the skirt and its
        // lowest stair must precede the next wall/stair by one full revolution.
        const double SampleTurn=(4+.37)/A->StepsPerTurn,Base=A->bFoundation?A->FoundationHeight:0;
        const double StepStart=4./A->StepsPerTurn,StepEnd=5./A->StepsPerTurn;
        const double Angle=FMath::DegreesToRadians(double(A->StartAngle))+(A->bClockwise?-1:1)*UE_TWO_PI*SampleTurn;
        const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
        auto Radius=[&](double Turn) { return FMath::Lerp(double(A->BaseRadius),double(A->TopRadius),Turn/A->Turns); };
        auto Sample=[&](double R,double Z,FVector2f& Range)
        {
            FIntPoint Cell;
            if (!TestTrue(TEXT("Entrance glow probe resolves to an actual fragment"),A->WorldLocationToCell(Direction*R+FVector(0,0,Z),Cell))) return false;
            const auto& P=A->Pieces[Cell.X];
            Range={FMath::Min(P.GlowUV[0].X,P.GlowUV[2].X),FMath::Max(P.GlowUV[0].X,P.GlowUV[2].X)};
            return true;
        };
        FVector2f Skirt,FirstTread,NextWall,NextTread;
        const double FirstTop=Base+A->SlabThickness+A->HeightAtTurn(StepEnd);
        const double HalfTreadWidth=(A->WalkwayWidth-A->WallThickness)*.5;
        if (!Sample(Radius(SampleTurn)-2,Base+A->HeightAtTurn(StepStart)*.5,Skirt) ||
            !Sample(Radius(SampleTurn)-HalfTreadWidth,FirstTop-1,FirstTread) ||
            !Sample(Radius(SampleTurn)-A->WalkwayWidth+A->WallThickness-2,FirstTop+40,NextWall) ||
            !Sample(Radius(SampleTurn+1)-HalfTreadWidth,Base+A->SlabThickness+A->HeightAtTurn(StepEnd+1)-1,NextTread)) return false;
        if (!TestTrue(TEXT("Lowest skirt and its stair illuminate together"),Skirt.Equals(FirstTread,.01f))) return false;
        if (!TestTrue(TEXT("Upper wall waits for its own supported stair"),NextWall.Equals(NextTread,.01f))) return false;
        if (!TestTrue(TEXT("Initial strip does not also light the next wall or stair"),NextWall.X>Skirt.Y+A->GlowTrailLength)) return false;
        // Inspect the actual UV stream used by the shader, including UE triangle winding.
        for (int32 PieceIndex:{0,A->Pieces.Num()/2})
        {
            const auto& P=A->Pieces[PieceIndex];
            UE::Geometry::FDynamicMesh3 Mesh;
            Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
            Mesh.Attributes()->SetNumUVLayers(2);Mesh.Attributes()->EnableMaterialID();
            A->AppendPiece(Mesh,P,2,true);
            const auto* UV=Mesh.Attributes()->GetUVLayer(1);
            for (int32 T:Mesh.TriangleIndicesItr())
            {
                const auto V=Mesh.GetTriangle(T),U=UV->GetTriangle(T);
                for (int32 I=0;I<3;++I)
                {
                    bool Matched=false;
                    for (int32 J=0;J<8;++J)
                        Matched|=FVector::Dist(Mesh.GetVertex(V[I]),P.V[J])<.2 && UV->GetElement(U[I]).Equals(P.GlowUV[J],.001f);
                    if (!TestTrue(TEXT("Shader path UV stays attached to the correct overlay vertex"),Matched)) return false;
                }
            }
        }
        return true;
    };
    if (!VerifyStairFlow()) return false;
    TestTrue(TEXT("Center shaft is wide enough to enter"),A->ClearShaftDiameter>=290);
    TestEqual(TEXT("Reference walls are upright"),A->WallInset,0.f);
    TestTrue(TEXT("Stair edges sit flush on the wall below, without a floating overhang"),
        FMath::IsNearlyEqual((A->BaseRadius-A->TopRadius)/A->Turns,A->WalkwayWidth-A->WallThickness,.001f));
    TestTrue(TEXT("Reference has a hollow square plinth"),A->bFoundation);
    for (const FVector2D P:{FVector2D(0,0),FVector2D(90,0),FVector2D(0,90),FVector2D(-90,-90)})
        TestFalse(TEXT("No hidden cap or core blocks the launch path"),Trace({P.X,P.Y,20},{P.X,P.Y,A->GeneratedHeight+100}));
    const double FoundationEdge=A->BaseRadius+A->FoundationMargin;
    const double VentX=(FoundationEdge-25-FMath::Max(80.f,A->WallThickness))/8;
    TestFalse(TEXT("Rectangular vents are real holes in the foundation collision"),
        Trace({VentX,-FoundationEdge-100,A->FoundationHeight*.4},{VentX,-FoundationEdge+250,A->FoundationHeight*.4}));
    TestTrue(TEXT("Stone lintel above the vent blocks shots"),
        Trace({VentX,-FoundationEdge-100,A->FoundationHeight*.85},{VentX,-FoundationEdge+250,A->FoundationHeight*.85}));
    // The entrance revolution must have masonry directly under its outside edge,
    // including the rear and the join into the next turn, not an inset floating support.
    for (int32 Step:{4,12,25,40,58,72,79})
    {
        // Sample the face interior, not a coincident radial seam between convex hulls.
        const double T=(Step+.37)/A->StepsPerTurn;
        const double Angle=FMath::DegreesToRadians(double(A->StartAngle))-UE_TWO_PI*T;
        const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const double Radius=FMath::Lerp(double(A->BaseRadius),double(A->TopRadius),T/A->Turns);
        const double Rise=A->HeightAtTurn(double(Step)/A->StepsPerTurn);
        for (double Fraction:{.1,.5,.9})
        {
            const FVector Z(0,0,A->FoundationHeight+Rise*Fraction);
            FHitResult Hit;
            const bool HitSkirt=World->LineTraceSingleByChannel(Hit,
                Z+Direction*(Radius+20),Z+Direction*(Radius-A->WallThickness-20),ECC_Visibility);
            if (!TestTrue(TEXT("First-turn outer wall reaches continuously from the plinth to the tread"),HitSkirt)) return false;
            TestTrue(FString::Printf(TEXT("Lower wall is flush with the stair's outer edge (step %d, height %.2f, radius %.3f, hit %.3f)"),
                Step,Fraction,Radius,FVector2D(Hit.ImpactPoint.X,Hit.ImpactPoint.Y).Size()),
                FMath::Abs(FVector2D(Hit.ImpactPoint.X,Hit.ImpactPoint.Y).Size()-Radius)<2.);
        }
    }
    for (const auto& C:A->ChunkComponents)
        for (int32 T:C->GetDynamicMesh()->GetMeshRef().TriangleIndicesItr())
            if (!TestTrue(TEXT("No degenerate wedge faces"),C->GetDynamicMesh()->GetMeshRef().GetTriArea(T)>1.e-8)) return false;
    // Test inside the actual room at stair elevations, not just the empty center axis.
    // Every ray must meet the same smooth masonry surface before reaching a tread.
    for (int32 Step=1;Step<400;Step+=17)
    {
        for (double Offset:{-.1,10.,30.})
        {
            const double Z=A->FoundationHeight+A->HeightAtTurn(double(Step)/A->StepsPerTurn)+Offset;
            for (int32 DirectionIndex=0;DirectionIndex<12;++DirectionIndex)
            {
                const double Angle=UE_TWO_PI*(DirectionIndex+.37)/12;
                const FVector Start(0,0,Z),Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
                FHitResult Hit;FIntPoint Cell;
                if (!TestTrue(TEXT("Interior wall is continuous at stair and riser elevations"),
                    World->LineTraceSingleByChannel(Hit,Start,Start+Direction*(A->BaseRadius+100),ECC_Visibility))) return false;
                if (!TestTrue(TEXT("Only smooth stone faces are exposed to the interior"),
                    A->ResolveExistingCellFromHit(Hit,Cell) && A->Pieces[Cell.X].Material==0 && A->Pieces[Cell.X].bSmoothInnerFace)) return false;
                if (!TestTrue(TEXT("Interior has no stair-shaped ledges or recesses"),
                    FMath::Abs(FVector2D(Hit.ImpactPoint.X,Hit.ImpactPoint.Y).Size()-A->InteriorRadiusAtHeight(Z))<.25)) return false;
            }
        }
    }
    bool HasHighCrown=false,BlocksRearOpening=false;
    const double HighCrownZ=A->FoundationHeight+A->HeightAtTurn(A->Turns)+A->SlabThickness+50;
    for (const auto& Piece:A->Pieces)
    {
        FVector Center=FVector::ZeroVector; for (const auto& V:Piece.V) Center+=V; Center/=8;
        if (!TestTrue(TEXT("Convex fragments contain their centers"),Piece.Contains(Center,.001))) return false;
        for (const auto& V:Piece.V) if (V.Z>HighCrownZ)
        {
            HasHighCrown=true;
            BlocksRearOpening|=V.Y>.01;
        }
    }
    TestTrue(TEXT("The elevated summit crescent exists"),HasHighCrown);
    TestFalse(TEXT("The summit crescent leaves its rear open"),BlocksRearOpening);
    // Exercise exactly the hit route used by the railgun and ball projectiles.
    for (int32 Material:{0,1})
    {
        int32 Target=INDEX_NONE;
        for (int32 I=0;I<A->Pieces.Num();++I)
        {
            const auto& Piece=A->Pieces[I];
            FVector C=FVector::ZeroVector; for (const auto& V:Piece.V) C+=V; C/=8;
            const double Base=A->bFoundation?A->FoundationHeight:0;
            double Low=0,High=A->Turns;
            for (int32 K=0;K<24;++K)
            {
                const double Mid=(Low+High)*.5;
                if (A->HeightAtTurn(Mid)<C.Z-Base-A->SlabThickness*.5) Low=Mid;else High=Mid;
            }
            const double Turn=(Low+High)*.5;
            const double OuterRadius=FMath::Lerp(double(A->BaseRadius),double(A->TopRadius),Turn/A->Turns);
            // The entrance stairs have a stone terrace underneath. Aim one turn higher,
            // at the exposed outside of a tread, so this ray really has air behind it.
            const double Radius=FVector2D(C.X,C.Y).Size();
            const bool ExposedTread=Turn>1.25 && Radius>OuterRadius-A->WalkwayWidth+A->WallThickness+10
                && Radius<OuterRadius-A->WallThickness-10;
            const bool ExposedWall=Piece.bSmoothInnerFace && Piece.Bounds.GetSize().Z>50 && C.Z>Base+100;
            if (Piece.Material==Material && (Material==0?ExposedWall:ExposedTread))
                { Target=I; break; }
        }
        if (!TestTrue(TEXT("Wall and tread targets exist"),Target!=INDEX_NONE)) return false;
        FVector Center=FVector::ZeroVector; for (const auto& V:A->Pieces[Target].V) Center+=V; Center/=8;
        FVector Start,End;
        if (Material==0)
        {
            const FVector Direction=FVector(Center.X,Center.Y,0).GetSafeNormal();
            double Inner=DBL_MAX,Outer=-DBL_MAX;
            for (const FVector& V:A->Pieces[Target].V)
            {
                Inner=FMath::Min(Inner,FVector::DotProduct(V,Direction));
                Outer=FMath::Max(Outer,FVector::DotProduct(V,Direction));
            }
            Start=FVector(0,0,Center.Z)+Direction*(Outer+10);
            End=FVector(0,0,Center.Z)+Direction*(Inner-10);
        }
        else
        {
            Start=FVector(Center.X,Center.Y,A->Pieces[Target].Bounds.Max.Z+10);
            End=FVector(Center.X,Center.Y,A->Pieces[Target].Bounds.Min.Z+.1);
        }
        FHitResult Hit;
        if (!TestTrue(TEXT("Shot hits the real convex collision"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility))) return false;
        FIntPoint Cell;
        if (!TestTrue(TEXT("Impact maps to intended fragment"),A->ResolveExistingCellFromHit(Hit,Cell) && Cell.X==Target)) return false;
        TestTrue(TEXT("Nonlethal hit applies damage"),A->DamageCellFromHit(Hit,5));
        TestEqual(TEXT("Nonlethal hit keeps mesh"),Triangles(),Original);
        TestTrue(TEXT("Lethal hit removes target"),A->DamageCellFromHit(Hit,100));
        A->ProcessQueuedDestructions(1.e10); A->FlushDirtyChunks();
        TestEqual(TEXT("Hit fragment and its glow both vanish"),Triangles(),Original-24);
        TestFalse(TEXT("A shot can pass through the new hole"),Trace(Start,End));
        A->RestoreCell({Target,0}); A->FlushDirtyChunks();
        TestEqual(TEXT("Repair restores fragment mesh"),Triangles(),Original);
        TestTrue(TEXT("Repair restores collision"),Trace(Start,End));
    }
    const FTransform Transform(FRotator(0,37,0),FVector(300,200,100),FVector(1.3,.8,1.5));
    A->SetActorTransform(Transform);
    FVector Center=FVector::ZeroVector; for (const auto& V:A->Pieces[7].V) Center+=V; Center/=8;
    FIntPoint Cell;
    TestTrue(TEXT("Scaled and rotated piece resolves correctly"),A->WorldLocationToCell(Transform.TransformPosition(Center),Cell) && Cell.X==7);
    TestTrue(TEXT("Explosion removes multiple nearby pieces"),A->DestroyCellsInRadius(Transform.TransformPosition(Center),250,EChunkedFloorBlastShape::Circle)>1);
    A->ProcessQueuedDestructions(1.e10); A->FlushDirtyChunks();
    TestTrue(TEXT("Explosion updates visible geometry"),Triangles()<Original-12);
    A->SetActorTransform(FTransform::Identity); A->RebuildTower();
    TestEqual(TEXT("Rebuild restores the original tower"),Triangles(),Original);
    A->Turns=1.25f; A->StepsPerTurn=48; A->SummitWallExtension=0; A->bClockwise=false; A->RebuildTower();
    TestTrue(TEXT("Counterclockwise fractional spiral generates"),A->GenerationWarning.IsEmpty() && A->GeneratedStepCount==60);
    if (!VerifyStairFlow()) return false;
    for (const auto& Piece:A->Pieces)
    {
        FVector C=FVector::ZeroVector; for (const auto& V:Piece.V) C+=V; C/=8;
        if (!TestTrue(TEXT("Counterclockwise winding is outward"),Piece.Contains(C,.001))) return false;
    }
    const int32 Before=Triangles();
    AddExpectedMessage(TEXT("Piece budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    A->MaxGeneratedCells=5; A->RebuildTower();
    TestEqual(TEXT("Budget failure preserves complete existing geometry"),Triangles(),Before);
    A->MaxGeneratedCells=30000; A->BaseRadius=600; A->TopRadius=250; A->WalkwayWidth=900; A->RebuildTower();
    TestTrue(TEXT("Extreme dimensions still preserve a hollow shaft"),A->GenerationWarning.IsEmpty() && A->ClearShaftDiameter>290);
    A->DispatchBeginPlay();
    TestTrue(TEXT("Runtime construction rebuilds geometry"),Triangles()>0);
    TestFalse(TEXT("Undamaged tower has no idle tick"),A->IsActorTickEnabled());
    AddInfo(FString::Printf(TEXT("Reference spiral: %d fragments, %d triangles"),Count,Original));
    return true;
}
#endif
