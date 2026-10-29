#include "ParametricLobbyPlatform.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Generators/SphereGenerator.h"
#include "Operations/MeshBoolean.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace LobbyPlatform
{
	constexpr int32 MaxPads = 16;
	constexpr int32 MaxSegments = 32;
	double Safe(double V, double Min, double Max, double Default)
	{
		return FMath::Clamp(FMath::IsFinite(V) ? V : Default, Min, Max);
	}
	FVector2D Dimensions(const AParametricLobbyPlatform& A)
	{
		return FVector2D(Safe(A.Width, 100, 1.e6, 3600), Safe(A.Depth, 100, 1.e6, 2800));
	}
	double Bevel(const AParametricLobbyPlatform& A)
	{
		const FVector2D D = Dimensions(A);
		return Safe(A.BevelSize, 0, FMath::Min(FMath::Min(D.X, D.Y) * .01,
			Safe(A.Thickness, 2, 1.e5, 80) * .1), 10);
	}
	double Rim(const AParametricLobbyPlatform& A)
	{
		const FVector2D D = Dimensions(A);
		return Safe(A.RimWidth, 0, FMath::Min(D.X, D.Y) * .04, 28);
	}
	void Warn(FString* W, const FString& Message)
	{
		if (W) { if (!W->IsEmpty()) { *W += TEXT("\n"); } *W += Message; }
	}
	FVector2D InsetVertex(const TArray<FVector2D>& Loop, int32 I, double Amount)
	{
		// Homothetic rings remain convex even when a corner radius is smaller than the rim.
		// A literal polygon offset would fold the tiny arcs inside out in that case.
		double Inradius = TNumericLimits<double>::Max();
		for (int32 J = 0; J < Loop.Num(); ++J)
		{
			const FVector2D E = (Loop[(J + 1) % Loop.Num()] - Loop[J]).GetSafeNormal();
			Inradius = FMath::Min(Inradius, FVector2D::DotProduct(FVector2D(E.Y, -E.X), Loop[J]));
		}
		return Loop[I] * FMath::Clamp(1.0 - Amount / FMath::Max(Inradius, 1.0), .1, 1.0);
	}
}

AParametricLobbyPlatform::AParametricLobbyPlatform(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	bEnableComputeMeshPool = false;
	GetDynamicMeshComponent()->SetCanEverAffectNavigation(true);
	GetDynamicMeshComponent()->bUseAsyncCooking = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Surface(
		TEXT("/Game/GameplayActors/LobbyPlatform/Materials/M_LobbySurface.M_LobbySurface"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Trim(
		TEXT("/Game/GameplayActors/LobbyPlatform/Materials/M_LobbyTrim.M_LobbyTrim"));
	if (Surface.Succeeded()) { SurfaceMaterial = Surface.Object; }
	if (Trim.Succeeded()) { TrimMaterial = Trim.Object; }
	ResetReferenceLayout();
}

void AParametricLobbyPlatform::ResetReferenceLayout()
{
	Pads.Reset(); Paths.Reset();
	auto Add = [this](const TCHAR* Id, FVector2D Position, float Radius, ELobbyPadPaint Paint)
	{
		FLobbyPlatformPad& P = Pads.AddDefaulted_GetRef();
		P.Id = Id; P.Position = Position; P.Radius = Radius; P.Paint = Paint;
	};
	Add(TEXT("Center"), FVector2D(0, 0), .38f, ELobbyPadPaint::Ring);
	Add(TEXT("Right"), FVector2D(-.66, -.48), .235f, ELobbyPadPaint::Ring);
	Add(TEXT("Left"), FVector2D(.65, .44), .245f, ELobbyPadPaint::Disc);
	Add(TEXT("Entrance"), FVector2D(.82, -.82), .19f, ELobbyPadPaint::Disc);
	Add(TEXT("Back"), FVector2D(-.82, .82), .19f, ELobbyPadPaint::Disc);
	Pads[3].bShowCircle = false;
	Pads[4].bShowCircle = false;
	for (const TCHAR* Id : {TEXT("Right"), TEXT("Left"), TEXT("Entrance"), TEXT("Back")})
	{
		FLobbyPlatformPath& Path = Paths.AddDefaulted_GetRef();
		Path.FromPad = TEXT("Center"); Path.ToPad = Id;
	}
	// CDO construction only installs data. Geometry needs registered instance components.
	if (!IsTemplate() && GetDynamicMeshComponent()->IsRegistered()) { RebuildPlatform(); }
}

void AParametricLobbyPlatform::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildPlatform();
}

void AParametricLobbyPlatform::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (!IsTemplate()) { RebuildPlatform(); }
}

void AParametricLobbyPlatform::BeginPlay()
{
	Super::BeginPlay();
	RebuildPlatform();
	if (bSpawnOnBeginPlay && HasAuthority()) { SpawnConfiguredActors(); }
}

void AParametricLobbyPlatform::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearSpawnedActors();
	Super::EndPlay(EndPlayReason);
}

TArray<FVector2D> AParametricLobbyPlatform::MakeOutline(TArray<FVector2D>* Core, double* Fillet) const
{
	using namespace LobbyPlatform;
	if (Core) { Core->Reset(); }
	if (Fillet) { *Fillet = 0; }
	const FVector2D Half = Dimensions(*this) * .5;
	const int32 Steps = FMath::Clamp(CornerSegments, 4, 32);
	TArray<FVector2D> Points;
	if (Shape == ELobbyPlatformShape::Ellipse)
	{
		for (int32 I = 0; I < Steps * 8; ++I)
		{
			const double A = UE_DOUBLE_TWO_PI * I / (Steps * 8);
			Points.Add(FVector2D(FMath::Cos(A) * Half.X, FMath::Sin(A) * Half.Y));
		}
		return Points;
	}
	if (Shape == ELobbyPlatformShape::RoundedRectangle || Shape == ELobbyPlatformShape::Rectangle)
	{
		Points = {FVector2D(Half.X, Half.Y), FVector2D(-Half.X, Half.Y),
			FVector2D(-Half.X, -Half.Y), FVector2D(Half.X, -Half.Y)};
	}
	else
	{
		const int32 N = Shape == ELobbyPlatformShape::Triangle ? 3 :
			Shape == ELobbyPlatformShape::Hexagon ? 6 : FMath::Clamp(PolygonSides, 3, 32);
		for (int32 I = 0; I < N; ++I)
		{
			const double A = UE_DOUBLE_PI * .5 + UE_DOUBLE_TWO_PI * I / N;
			Points.Add(FVector2D(FMath::Cos(A) * Half.X, FMath::Sin(A) * Half.Y));
		}
	}
	double Radius = Shape == ELobbyPlatformShape::Rectangle ? 0 : Safe(CornerRadius, 0, FMath::Min(Half.X, Half.Y) * .9, 300);
	if (Radius < .001) { if (Core) { *Core = Points; } return Points; }
	// A common circular fillet radius, limited by the shortest adjacent edge.
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const FVector2D A = Points[(I + Points.Num() - 1) % Points.Num()] - Points[I];
		const FVector2D B = Points[(I + 1) % Points.Num()] - Points[I];
		const double Angle = FMath::Acos(FMath::Clamp(FVector2D::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0));
		Radius = FMath::Min(Radius, FMath::Min(A.Size(), B.Size()) * .45 * FMath::Tan(Angle * .5));
	}
	TArray<FVector2D> Rounded;
	if (Fillet) { *Fillet = Radius; }
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const FVector2D P = Points[I];
		const FVector2D A = (Points[(I + Points.Num() - 1) % Points.Num()] - P).GetSafeNormal();
		const FVector2D B = (Points[(I + 1) % Points.Num()] - P).GetSafeNormal();
		const double Angle = FMath::Acos(FMath::Clamp(FVector2D::DotProduct(A, B), -1.0, 1.0));
		const FVector2D Center = P + (A + B).GetSafeNormal() * (Radius / FMath::Sin(Angle * .5));
		if (Core) { Core->Add(Center); }
		const FVector2D Start = P + A * (Radius / FMath::Tan(Angle * .5)) - Center;
		const double StartAngle = FMath::Atan2(Start.Y, Start.X);
		for (int32 J = 0; J <= Steps; ++J)
		{
			const double T = StartAngle + (UE_DOUBLE_PI - Angle) * J / Steps;
			Rounded.Add(Center + FVector2D(FMath::Cos(T), FMath::Sin(T)) * Radius);
		}
	}
	return Rounded;
}

void AParametricLobbyPlatform::ResolvePads(TArray<FResolvedPad>& Result, FString* Warning) const
{
	using namespace LobbyPlatform;
	Result.Reset();
	TArray<FVector2D> Outline = MakeOutline();
	const FVector2D Half = Dimensions(*this) * .5;
	const double Inset = Bevel(*this) + Rim(*this) + 1;
	const TArray<FVector2D> Outer = Outline;
	for (int32 I = 0; I < Outline.Num(); ++I) { Outline[I] = InsetVertex(Outer, I, Inset); }
	double Inradius = TNumericLimits<double>::Max();
	for (int32 I = 0; I < Outline.Num(); ++I)
	{
		const FVector2D E = (Outline[(I + 1) % Outline.Num()] - Outline[I]).GetSafeNormal();
		Inradius = FMath::Min(Inradius, FVector2D::DotProduct(FVector2D(E.Y, -E.X), Outline[I]));
	}
	if (Pads.Num() > MaxPads) { Warn(Warning, TEXT("Only the first 16 pads are supported.")); }
	TMap<FName, int32> Counts;
	for (const FLobbyPlatformPad& P : Pads) { Counts.FindOrAdd(P.Id)++; }
	for (int32 I = 0; I < FMath::Min(Pads.Num(), MaxPads); ++I)
	{
		const FLobbyPlatformPad& P = Pads[I];
		if (!P.bEnabled) { continue; }
		if (P.Id.IsNone() || Counts[P.Id] != 1)
		{
			Warn(Warning, FString::Printf(TEXT("Pad %d has an empty or duplicate ID; skipped."), I)); continue;
		}
		FVector2D Center(Safe(P.Position.X, -1, 1, 0) * Half.X, Safe(P.Position.Y, -1, 1, 0) * Half.Y);
		double Radius = Safe(P.Radius, .001, .95, .22) * FMath::Min(Half.X, Half.Y);
		if (!P.bShowCircle) { Result.Add({I, Center, 0}); continue; }
		if (bFitPadsInsidePlatform) { Radius = FMath::Min(Radius, FMath::Max(1.0, Inradius * .95)); }
		double Fit = 1;
		bool bInside = true;
		for (int32 J = 0; J < Outline.Num(); ++J)
		{
			const FVector2D E = (Outline[(J + 1) % Outline.Num()] - Outline[J]).GetSafeNormal();
			const FVector2D N(E.Y, -E.X);
			const double Limit = FVector2D::DotProduct(N, Outline[J]) - Radius;
			const double Projection = FVector2D::DotProduct(N, Center);
			bInside &= Projection <= Limit + .001;
			if (Projection > 0) { Fit = FMath::Min(Fit, Limit / Projection); }
		}
		if (!bInside && !bFitPadsInsidePlatform)
		{
			Warn(Warning, FString::Printf(TEXT("Pad %s is outside the inner surface; skipped. Enable Fit Pads or move it inside."), *P.Id.ToString())); continue;
		}
		Center *= FMath::Clamp(Fit, 0.0, 1.0);
		Result.Add({I, Center, Radius});
	}
}

const FLobbyPlatformPad* AParametricLobbyPlatform::FindResolvedPad(FName Id, FResolvedPad& Result) const
{
	TArray<FResolvedPad> Resolved;
	ResolvePads(Resolved);
	for (const FResolvedPad& P : Resolved)
	{
		if (Pads[P.Index].Id == Id && Pads[P.Index].bShowCircle) { Result = P; return &Pads[P.Index]; }
	}
	return nullptr;
}

void AParametricLobbyPlatform::RebuildPlatform()
{
	if (IsTemplate()) { return; }
	using namespace LobbyPlatform;
	const FVector2D D = Dimensions(*this);
	const double T = Safe(Thickness, 2, 1.e5, 80), B = Bevel(*this), R = Rim(*this);
	const TArray<FVector2D> Outline = MakeOutline();
	FDynamicMesh3 Mesh;
	Mesh.EnableAttributes(); Mesh.Attributes()->EnableMaterialID(); Mesh.Attributes()->SetNumUVLayers(1);
	auto* Normals = Mesh.Attributes()->PrimaryNormals();
	auto* UVs = Mesh.Attributes()->PrimaryUV();
	auto Tri = [&](int32 A, int32 BB, int32 C, int32 Slot)
	{
		const int32 Id = Mesh.AppendTriangle(A, C, BB);
		if (Id < 0) { return; }
		Mesh.Attributes()->GetMaterialID()->SetValue(Id, Slot);
		const FVector3f N(Mesh.GetTriNormal(Id));
		FIndex3i NI, UI;
		const FIndex3i V(A, C, BB);
		for (int32 K = 0; K < 3; ++K)
		{
			NI[K] = Normals->AppendElement(N);
			const FVector3d P = Mesh.GetVertex(V[K]);
			UI[K] = UVs->AppendElement(FVector2f(P.X / D.X + .5, P.Y / D.Y + .5));
		}
		Normals->SetTriangle(Id, NI); UVs->SetTriangle(Id, UI);
	};
	auto Ring = [&](double Inset, double Z)
	{
		TArray<int32> V;
		for (int32 I = 0; I < Outline.Num(); ++I)
		{
			const FVector2D P = InsetVertex(Outline, I, Inset);
			V.Add(Mesh.AppendVertex(FVector3d(P.X, P.Y, Z)));
		}
		return V;
	};
	TArray<int32> Current = Ring(B + R, 0);
	const int32 Center = Mesh.AppendVertex(FVector3d::Zero());
	for (int32 I = 0; I < Outline.Num(); ++I) { Tri(Center, Current[I], Current[(I + 1) % Outline.Num()], 0); }
	auto NextRing = [&](double Inset, double Z, int32 Slot)
	{
		TArray<int32> Next = Ring(Inset, Z);
		for (int32 I = 0; I < Outline.Num(); ++I)
		{
			const int32 J = (I + 1) % Outline.Num();
			Tri(Current[I], Next[I], Next[J], Slot); Tri(Current[I], Next[J], Current[J], Slot);
		}
		Current = MoveTemp(Next);
	};
	if (R > 0) { NextRing(B, 0, 2); }
	if (B > 0) { NextRing(0, -B, 2); }
	const double LayerHalf = Safe(NeonLayerThickness, .01, .5, .18) * .5;
	const double LayerCenter = Safe(NeonLayerPosition, .15 + LayerHalf, .85 - LayerHalf, .72);
	NextRing(0, -T * (LayerCenter - LayerHalf), 1);
	NextRing(0, -T * (LayerCenter + LayerHalf), 3);
	NextRing(0, -T + B, 1);
	if (B > 0) { NextRing(B, -T, 1); }
	const int32 Bottom = Mesh.AppendVertex(FVector3d(0, 0, -T));
	for (int32 I = 0; I < Outline.Num(); ++I) { Tri(Bottom, Current[(I + 1) % Outline.Num()], Current[I], 1); }
	for (const FTransform& Cut : DamageCuts) { CutMesh(Mesh, Cut); }
	UpdateCutSurfaceAttributes(Mesh);
	PublishMesh(MoveTemp(Mesh));
}

void AParametricLobbyPlatform::PublishMesh(FDynamicMesh3&& Mesh)
{
	UDynamicMeshComponent* C = GetDynamicMeshComponent();
	C->SetTransientDeferCollisionUpdates(true);
	C->SetMesh(MoveTemp(Mesh));
	UpdateMaterials();
	C->SetCollisionEnabled(bEnableCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	C->SetTransientDeferCollisionUpdates(false);
	C->SetComplexAsSimpleCollisionEnabled(bEnableCollision, true);
}

void AParametricLobbyPlatform::UpdateMaterials()
{
	using namespace LobbyPlatform;
	MaterialInstances.SetNum(5);
	for (int32 I = 0; I < 5; ++I)
	{
		UMaterialInterface* Parent = I == 0 ? SurfaceMaterial.Get() : TrimMaterial.Get();
		if (!Parent) { Parent = UMaterial::GetDefaultMaterial(MD_Surface); }
		if (!MaterialInstances[I] || MaterialInstances[I]->Parent != Parent)
		{
			MaterialInstances[I] = UMaterialInstanceDynamic::Create(Parent, this);
			MaterialInstances[I]->SetFlags(RF_Transient);
		}
		UMaterialInstanceDynamic* M = MaterialInstances[I];
		M->SetVectorParameterValue(TEXT("Color"), I == 0 ? SurfaceColor : I == 2 ? RimColor : I == 3 ? CyanColor : BodyColor);
		M->SetScalarParameterValue(TEXT("Roughness"), I == 0 ? Safe(SurfaceRoughness, .02, 1, .19) : .24);
		M->SetScalarParameterValue(TEXT("Metallic"), I == 0 ? Safe(SurfaceMetallic, 0, 1, .32) : .42);
		M->SetScalarParameterValue(TEXT("Emission"), I == 0 ? Safe(SurfaceEmission, 0, 20, .18) : I == 3 ? Safe(NeonIntensity, 0, 100, 8) : 0);
		M->SetScalarParameterValue(TEXT("InteriorEnabled"), I == 4 ? 1 : 0);
		if (I == 4)
		{
			const double Half = Safe(NeonLayerThickness, .01, .5, .18) * .5;
			M->SetVectorParameterValue(TEXT("NeonColor"), CyanColor);
			M->SetScalarParameterValue(TEXT("NeonIntensity"), Safe(NeonIntensity, 0, 100, 8));
			M->SetScalarParameterValue(TEXT("LayerCenter"), Safe(NeonLayerPosition, .15 + Half, .85 - Half, .72));
			M->SetScalarParameterValue(TEXT("LayerHalfWidth"), Half);
		}
		GetDynamicMeshComponent()->SetMaterial(I, M);
	}
	LayoutWarning.Reset();
	TArray<FResolvedPad> Resolved;
	ResolvePads(Resolved, &LayoutWarning);
	UMaterialInstanceDynamic* M = MaterialInstances[0];
	const FVector2D D = Dimensions(*this);
	M->SetVectorParameterValue(TEXT("Dimensions"), FLinearColor(D.X, D.Y, 0, 0));
	M->SetVectorParameterValue(TEXT("YellowColor"), YellowColor);
	M->SetScalarParameterValue(TEXT("YellowEmission"), Safe(YellowEmission, 0, 20, .55));
	M->SetScalarParameterValue(TEXT("ShowLayout"), bShowYellowLayout ? 1 : 0);
	TArray<FResolvedPad> Painted = Resolved.FilterByPredicate([&](const FResolvedPad& P) { return Pads[P.Index].bShowCircle; });
	M->SetScalarParameterValue(TEXT("PadCount"), Painted.Num());
	TArray<FVector2D> Core;
	double Fillet;
	const TArray<FVector2D> Outline = MakeOutline(&Core, &Fillet);
	const double Scale = InsetVertex(Outline, 0, Bevel(*this) + Rim(*this)).Size() / Outline[0].Size();
	M->SetVectorParameterValue(TEXT("OutlineData"), FLinearColor(D.X * .5 * Scale, D.Y * .5 * Scale, Fillet * Scale,
		Shape == ELobbyPlatformShape::Ellipse ? 1 : 0));
	M->SetScalarParameterValue(TEXT("CoreCount"), Core.Num());
	for (int32 I = 0; I < Core.Num(); ++I)
	{
		M->SetVectorParameterValue(FName(*FString::Printf(TEXT("Core%d"), I)), FLinearColor(Core[I].X * Scale, Core[I].Y * Scale, 0, 0));
	}
	for (int32 I = 0; I < MaxPads; ++I)
	{
		FLinearColor Data(0, 0, 0, 0);
		if (Painted.IsValidIndex(I))
		{
			const FResolvedPad& P = Painted[I];
			const FLobbyPlatformPad& Settings = Pads[P.Index];
			const double Inner = Settings.Paint == ELobbyPadPaint::Ring ? P.Radius * (1 - Safe(Settings.RingWidth, .01, 1, .14)) : 0;
			Data = FLinearColor(P.Center.X, P.Center.Y, P.Radius, Inner);
		}
		M->SetVectorParameterValue(FName(*FString::Printf(TEXT("Pad%d"), I)), Data);
	}
	int32 SegmentCount = 0;
	for (const FLobbyPlatformPath& Path : Paths)
	{
		if (!Path.bEnabled) { continue; }
		const FResolvedPad* From = Resolved.FindByPredicate([&](const FResolvedPad& P) { return Pads[P.Index].Id == Path.FromPad; });
		const FResolvedPad* To = Resolved.FindByPredicate([&](const FResolvedPad& P) { return Pads[P.Index].Id == Path.ToPad; });
		if (!From || !To || From == To)
		{
			Warn(&LayoutWarning, FString::Printf(TEXT("Path %s -> %s has missing, disabled or identical endpoints; skipped."), *Path.FromPad.ToString(), *Path.ToPad.ToString())); continue;
		}
		TArray<FVector2D> Points = {From->Center};
		for (FVector2D P : Path.Waypoints) { Points.Add(FVector2D(Safe(P.X, -1, 1, 0) * D.X * .5, Safe(P.Y, -1, 1, 0) * D.Y * .5)); }
		Points.Add(To->Center);
		const bool bEdgeEnd = !Pads[To->Index].bShowCircle;
		if (bEdgeEnd)
		{
			const FVector2D Direction = (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal();
			if (Direction.IsNearlyZero()) { Warn(&LayoutWarning, TEXT("An edge arm needs a nonzero direction.")); continue; }
			Points.Last() = Points[Points.Num() - 2] + Direction * (D.Size() * 3.0);
		}
		const double HalfWidth = Safe(Path.Width, .001, .5, .055) * FMath::Min(D.X, D.Y) * .5;
		// Stop at the pad perimeter so ring interiors remain glossy purple like the reference.
		const FVector2D FirstDirection = (Points[1] - Points[0]).GetSafeNormal();
		const FVector2D LastDirection = (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal();
		const double FirstDistance = (Points[1] - Points[0]).Size();
		const double LastDistance = (Points.Last() - Points[Points.Num() - 2]).Size();
		if (Points.Num() == 2 && FirstDistance < From->Radius + To->Radius) { continue; }
		Points[0] += FirstDirection * FMath::Min(From->Radius, FirstDistance * .99);
		Points.Last() -= LastDirection * FMath::Min(To->Radius, LastDistance * .99);
		for (int32 I = 0; I + 1 < Points.Num(); ++I)
		{
			if (SegmentCount >= MaxSegments) { Warn(&LayoutWarning, TEXT("32 path segment limit reached; remaining segments skipped.")); break; }
			if ((Points[I + 1] - Points[I]).SizeSquared() < .001) { continue; }
			M->SetVectorParameterValue(FName(*FString::Printf(TEXT("Segment%d"), SegmentCount)),
				FLinearColor(Points[I].X, Points[I].Y, Points[I + 1].X, Points[I + 1].Y));
			M->SetScalarParameterValue(FName(*FString::Printf(TEXT("PathWidth%d"), SegmentCount)), HalfWidth);
			M->SetScalarParameterValue(FName(*FString::Printf(TEXT("PathInset%d"), SegmentCount)), bEdgeEnd ? Safe(Path.EdgeInset, 0, FMath::Min(D.X, D.Y) * .4, 65) : -1);
			++SegmentCount;
		}
	}
	M->SetScalarParameterValue(TEXT("SegmentCount"), SegmentCount);
}

bool AParametricLobbyPlatform::GetPadWorldCenter(FName PadId, FVector& WorldCenter) const
{
	WorldCenter = FVector::ZeroVector;
	FResolvedPad P;
	if (!FindResolvedPad(PadId, P)) { return false; }
	WorldCenter = GetActorTransform().TransformPosition(FVector(P.Center.X, P.Center.Y, 0));
	return true;
}

bool AParametricLobbyPlatform::GetPadSpawnTransform(FName PadId, FTransform& WorldTransform) const
{
	WorldTransform = FTransform::Identity;
	FResolvedPad P;
	const FLobbyPlatformPad* Settings = FindResolvedPad(PadId, P);
	if (!Settings || Settings->SpawnOffset.ContainsNaN() || Settings->SpawnRotation.ContainsNaN() || Settings->SpawnScale.ContainsNaN()) { return false; }
	const FTransform Local(Settings->SpawnRotation, FVector(P.Center.X, P.Center.Y, 0) + Settings->SpawnOffset, Settings->SpawnScale);
	WorldTransform = Local * GetActorTransform();
	return true;
}

TArray<FName> AParametricLobbyPlatform::GetPadIds() const
{
	TArray<FResolvedPad> Resolved; ResolvePads(Resolved);
	TArray<FName> Ids;
	for (const FResolvedPad& P : Resolved) { if (Pads[P.Index].bShowCircle) { Ids.Add(Pads[P.Index].Id); } }
	return Ids;
}

AActor* AParametricLobbyPlatform::GetSpawnedActorAtPad(FName PadId) const
{
	const TObjectPtr<AActor>* Found = SpawnedActors.Find(PadId);
	return Found && IsValid(Found->Get()) ? Found->Get() : nullptr;
}

AActor* AParametricLobbyPlatform::SpawnActorAtPad(FName PadId, TSubclassOf<AActor> ActorClass)
{
	if (!GetWorld() || !GetWorld()->IsGameWorld() || !HasAuthority()) { return nullptr; }
	FResolvedPad P;
	const FLobbyPlatformPad* Settings = FindResolvedPad(PadId, P);
	if (!Settings) { return nullptr; }
	if (!IsPadSurfaceIntact(PadId)) { return nullptr; }
	if (AActor* Existing = GetSpawnedActorAtPad(PadId)) { return Existing; }
	UClass* Class = ActorClass ? ActorClass.Get() : Settings->ActorClass.Get();
	if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
		Class->IsChildOf(AParametricLobbyPlatform::StaticClass())) { return nullptr; }
	FTransform Transform;
	if (!GetPadSpawnTransform(PadId, Transform)) { return nullptr; }
	// Reserve the slot before FinishSpawning: child BeginPlay may call back into this platform.
	if (SpawnedActors.Contains(PadId) && !SpawnedActors[PadId]) { return nullptr; }
	SpawnedActors.Add(PadId, nullptr);
	FActorSpawnParameters Params;
	Params.Owner = this; Params.SpawnCollisionHandlingOverride = SpawnCollisionHandling;
	Params.bDeferConstruction = true;
	AActor* Spawned = GetWorld()->SpawnActor<AActor>(Class, Transform, Params);
	if (!IsValid(Spawned)) { SpawnedActors.Remove(PadId); return nullptr; }
	SpawnedActors[PadId] = Spawned;
	Spawned->FinishSpawning(Transform);
	if (!IsValid(Spawned)) { SpawnedActors.Remove(PadId); return nullptr; }
	if (bAttachSpawnedActors && Spawned->GetRootComponent()) { Spawned->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform); }
	return Spawned;
}

TArray<AActor*> AParametricLobbyPlatform::SpawnConfiguredActors()
{
	TArray<AActor*> Result;
	for (const FName Id : GetPadIds()) { if (AActor* A = SpawnActorAtPad(Id, nullptr)) { Result.Add(A); } }
	return Result;
}

bool AParametricLobbyPlatform::ClearSpawnedActorAtPad(FName PadId)
{
	AActor* Existing = GetSpawnedActorAtPad(PadId);
	SpawnedActors.Remove(PadId);
	return Existing && Existing->Destroy();
}

void AParametricLobbyPlatform::ClearSpawnedActors()
{
	TArray<FName> Ids; SpawnedActors.GetKeys(Ids);
	for (const FName Id : Ids) { ClearSpawnedActorAtPad(Id); }
}

bool AParametricLobbyPlatform::CutMesh(FDynamicMesh3& Mesh, const FTransform& LocalSphere) const
{
	if (Mesh.TriangleCount() == 0) { return false; }
	FSphereGenerator Generator;
	Generator.Radius = 1; Generator.NumPhi = 16; Generator.NumTheta = 32; Generator.Generate();
	FDynamicMesh3 Cutter(&Generator);
	Cutter.EnableAttributes(); Cutter.Attributes()->EnableMaterialID();
	for (int32 V : Cutter.VertexIndicesItr()) { Cutter.SetVertex(V, LocalSphere.TransformPosition(Cutter.GetVertex(V))); }
	for (int32 T : Cutter.TriangleIndicesItr()) { Cutter.Attributes()->GetMaterialID()->SetValue(T, 4); }
	FDynamicMesh3 Result;
	FMeshBoolean Boolean(&Mesh, &Cutter, &Result, FMeshBoolean::EBooleanOp::Difference);
	Boolean.bSimplifyAlongNewEdges = true;
	Boolean.bPreserveTriangleGroups = true;
	// Commit atomically; an unsuccessful boolean must never replace good render/collision data.
	if (!Boolean.Compute() || !Result.CheckValidity() || (Result.TriangleCount() && !Result.IsClosed())) { return false; }
	Mesh = MoveTemp(Result);
	return true;
}

void AParametricLobbyPlatform::UpdateCutSurfaceAttributes(FDynamicMesh3& Mesh) const
{
	if (!Mesh.HasAttributes()) { return; }
	auto* UVs = Mesh.Attributes()->PrimaryUV();
	auto* Normals = Mesh.Attributes()->PrimaryNormals();
	const double T = LobbyPlatform::Safe(Thickness, 2, 1.e5, 80);
	for (int32 Triangle : Mesh.TriangleIndicesItr())
	{
		if (Mesh.Attributes()->GetMaterialID()->GetValue(Triangle) != 4) { continue; }
		const FIndex3i V = Mesh.GetTriangle(Triangle);
		FIndex3i U, N;
		for (int32 K = 0; K < 3; ++K)
		{
			const FVector3d P = Mesh.GetVertex(V[K]);
			U[K] = UVs->AppendElement(FVector2f(P.X / T, -P.Z / T));
			N[K] = Normals->AppendElement(FVector3f(Mesh.GetTriNormal(Triangle)));
		}
		UVs->SetTriangle(Triangle, U); Normals->SetTriangle(Triangle, N);
	}
}

bool AParametricLobbyPlatform::RemovePlatformSphere(FVector WorldLocation, float Radius)
{
	if (IsTemplate() || !bDestructible || !CanBeDamaged() || !HasAuthority() || WorldLocation.ContainsNaN() ||
		!FMath::IsFinite(Radius) || Radius <= 0 || !GetDynamicMeshComponent()) { return false; }
	const FVector Scale = GetActorScale3D().GetAbs();
	if (Scale.GetMin() < .0001) { return false; }
	const FVector LocalCenter = GetActorTransform().InverseTransformPosition(WorldLocation);
	const FVector LocalRadii = FVector(Radius) / Scale;
	FDynamicMesh3 Mesh(*GetDynamicMeshComponent()->GetMesh());
	// Broad-phase rejection also prevents storing irrelevant blast volumes.
	const FAxisAlignedBox3d CutBounds(LocalCenter - LocalRadii, LocalCenter + LocalRadii);
	if (!Mesh.GetBounds().Intersects(CutBounds)) { return false; }
	const FTransform Cut(FQuat::Identity, LocalCenter, LocalRadii);
	if (!CutMesh(Mesh, Cut)) { return false; }
	DamageCuts.Add(Cut); DamageCutCount = DamageCuts.Num();
	UpdateCutSurfaceAttributes(Mesh);
	const bool bEmpty = Mesh.TriangleCount() == 0;
	PublishMesh(MoveTemp(Mesh));
	// Actors over a newly opened hole can fall/move normally instead of hanging from an attachment.
	for (const auto& Pair : SpawnedActors)
	{
		if (IsValid(Pair.Value) && !IsPadSurfaceIntact(Pair.Key)) { Pair.Value->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); }
	}
	if (bEmpty && GetWorld() && GetWorld()->IsGameWorld()) { Destroy(); }
	return true;
}

float AParametricLobbyPlatform::TakeDamage(float DamageAmount, const FDamageEvent& Event,
	AController* EventInstigator, AActor* Causer)
{
	if (!bDestructible || !CanBeDamaged() || !HasAuthority() || !FMath::IsFinite(DamageAmount) ||
		DamageAmount < LobbyPlatform::Safe(MinimumDamage, .01, 1.e9, 1)) { return 0; }
	FVector Location = GetActorLocation();
	float Radius = LobbyPlatform::Safe(PointDamageRadius, 1, 1.e6, 110);
	if (Event.IsOfType(FPointDamageEvent::ClassID))
	{
		Location = static_cast<const FPointDamageEvent&>(Event).HitInfo.ImpactPoint;
	}
	else if (Event.IsOfType(FRadialDamageEvent::ClassID))
	{
		const FRadialDamageEvent& Radial = static_cast<const FRadialDamageEvent&>(Event);
		Location = Radial.Origin; Radius = Radial.Params.OuterRadius;
	}
	if (!RemovePlatformSphere(Location, Radius)) { return 0; }
	Super::TakeDamage(DamageAmount, Event, EventInstigator, Causer);
	return DamageAmount;
}

bool AParametricLobbyPlatform::DestroyPlatformCompletely()
{
	return bDestructible && CanBeDamaged() && HasAuthority() && !IsTemplate() && Destroy();
}

void AParametricLobbyPlatform::RepairPlatform()
{
	DamageCuts.Reset(); DamageCutCount = 0;
	RebuildPlatform();
}

bool AParametricLobbyPlatform::IsPadSurfaceIntact(FName PadId) const
{
	FResolvedPad P;
	if (!FindResolvedPad(PadId, P)) { return false; }
	const FDynamicMesh3* Mesh = GetDynamicMeshComponent()->GetMesh();
	if (!Mesh || Mesh->TriangleCount() == 0) { return false; }
	FDynamicMeshAABBTree3 Tree(Mesh, true);
	const FRay3d Ray(FVector3d(P.Center.X, P.Center.Y, 1), FVector3d(0, 0, -1));
	const int32 Triangle = Tree.FindNearestHitTriangle(Ray);
	// A cavity floor is not the original pad's surface, even if it still has a bottom.
	if (Triangle < 0) { return false; }
	const FIndex3i V = Mesh->GetTriangle(Triangle);
	return FMath::Abs(Mesh->GetVertex(V.A).Z) < .01 && FMath::Abs(Mesh->GetVertex(V.B).Z) < .01 && FMath::Abs(Mesh->GetVertex(V.C).Z) < .01;
}
