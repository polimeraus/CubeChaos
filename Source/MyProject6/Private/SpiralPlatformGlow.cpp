#include "SpiralPlatformGlow.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;

int32 USpiralPlatformGlow::SliceAtTime(double Elapsed, int32 Count,
	const FSpiralPlatformGlowSettings& InSettings, bool bSectionsAscend)
{
	if (!InSettings.bEnabled || Count <= 0 || !FMath::IsFinite(Elapsed) || Elapsed < 0) { return INDEX_NONE; }
	const double Duration = FMath::IsFinite(InSettings.SliceDuration) ? FMath::Clamp(double(InSettings.SliceDuration), .05, 10.) : .08;
	const double TravelTime = Count * Duration;
	const double Delay = FMath::IsFinite(InSettings.LoopDelay) ? FMath::Max(double(InSettings.LoopDelay), 0.) : 0.;
	const double Phase = InSettings.bLoop ? FMath::Fmod(Elapsed, TravelTime + Delay) : Elapsed;
	if (Phase >= TravelTime) { return INDEX_NONE; }
	const int32 Step = FMath::Clamp(FMath::FloorToInt(Phase / Duration), 0, Count - 1);
	const bool bForward = (InSettings.Direction == ESpiralPlatformGlowDirection::BottomToTop) == bSectionsAscend;
	return bForward ? Step : Count - 1 - Step;
}

void USpiralPlatformGlow::Start(AActor* InOwner, const FSpiralPlatformGlowSettings& InSettings,
	UMaterialInterface* DefaultMaterial, int32 InSliceCount, bool bSectionsAscend,
	TFunction<void(FDynamicMesh3&, int32)> InBuildSlice)
{
	Release();
	if (!InOwner || !InOwner->GetWorld() || !InSettings.bEnabled || InSliceCount <= 0) { return; }
	Owner = InOwner;
	Settings = InSettings;
	Settings.SliceDuration = FMath::IsFinite(Settings.SliceDuration) ? FMath::Clamp(Settings.SliceDuration, .05f, 10.f) : .08f;
	Settings.LoopDelay = FMath::IsFinite(Settings.LoopDelay) ? FMath::Max(Settings.LoopDelay, 0.f) : 0.f;
	SliceCount = InSliceCount;
	bAscendingSections = bSectionsAscend;
	BuildSlice = MoveTemp(InBuildSlice);
	UMaterialInterface* Material = Settings.Material ? Settings.Material.Get() : DefaultMaterial;
	if (!Material) { Release(); return; }
	const float Intensity = FMath::IsFinite(Settings.GlowIntensity) ? FMath::Clamp(Settings.GlowIntensity, 0.f, 1000.f) : 8.f;
	const bool bSimpleGlow = Material->GetMaterial()->GetPathName() ==
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow");
	FName IntensityParameter = NAME_None;
	float PreviousIntensity = 0;
	if (!Settings.IntensityParameterName.IsNone())
	{
		if (Material->GetScalarParameterValue(FMaterialParameterInfo(Settings.IntensityParameterName), PreviousIntensity))
		{ IntensityParameter = Settings.IntensityParameterName; }
	}
	else
	{
		for (FName Name : { FName(TEXT("Glow Intensity")), FName(TEXT("GlowIntensity")), FName(TEXT("EmissiveIntensity")), FName(TEXT("Intensity")) })
		{
			if (Material->GetScalarParameterValue(FMaterialParameterInfo(Name), PreviousIntensity))
			{ IntensityParameter = Name; break; }
		}
	}
	FLinearColor SimpleGlowColor = FLinearColor::White;
	if (bSimpleGlow && Settings.Material)
	{
		FLinearColor ExistingColor;
		if (Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), ExistingColor) &&
			FMath::Max3(ExistingColor.R, ExistingColor.G, ExistingColor.B) > 0.f)
		{ SimpleGlowColor = ExistingColor; }
	}
	if (bSimpleGlow || !IntensityParameter.IsNone())
	{
		auto* Glow = UMaterialInstanceDynamic::Create(Material, this);
		// AliasReference defaults to zero emission. Drive only the glow scalar on a
		// private instance; preserve the selected material's color, maps and source asset.
		if (!IntensityParameter.IsNone()) { Glow->SetScalarParameterValue(IntensityParameter, Intensity); }
		if (bSimpleGlow)
		{
			Glow->SetVectorParameterValue(TEXT("Color"), SimpleGlowColor * Intensity);
			Glow->SetScalarParameterValue(TEXT("Custom Prim Switch"), 1.f);
		}
		Material = Glow;
	}
	Surface = NewObject<UDynamicMeshComponent>(InOwner, NAME_None, RF_Transient);
	InOwner->AddInstanceComponent(Surface);
	Surface->SetupAttachment(InOwner->GetRootComponent());
	Surface->SetMobility(EComponentMobility::Movable);
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Surface->SetGenerateOverlapEvents(false);
	Surface->SetCanEverAffectNavigation(false);
	Surface->SetCastShadow(false);
	Surface->SetReceivesDecals(false);
	Surface->ComponentTags.Add(TEXT("SpiralPlatformGlow"));
	Surface->SetMaterial(0, Material);
	if (bSimpleGlow)
	{
		// This material reads Custom Primitive Data, not MID overrides, for these
		// inputs. Select that branch and supply RGB brightness plus opaque alpha.
		Surface->SetCustomPrimitiveDataFloat(0, 1.f);
		Surface->SetCustomPrimitiveDataVector4(1, FVector4(SimpleGlowColor.R * Intensity,
			SimpleGlowColor.G * Intensity, SimpleGlowColor.B * Intensity, 1.f));
	}
	Surface->RegisterComponent();
	StartTime = InOwner->GetWorld()->GetTimeSeconds();
	Update();
	InOwner->GetWorld()->GetTimerManager().SetTimer(Timer, this, &USpiralPlatformGlow::Update, Settings.SliceDuration, true);
}

void USpiralPlatformGlow::Update()
{
	if (!Owner.IsValid() || !Owner->GetWorld()) { Release(); return; }
	const double Elapsed = Owner->GetWorld()->GetTimeSeconds() - StartTime;
	RenderSlice(SliceAtTime(Elapsed, SliceCount, Settings, bAscendingSections));
	if (!Settings.bLoop && Elapsed >= SliceCount * double(Settings.SliceDuration))
	{
		Owner->GetWorld()->GetTimerManager().ClearTimer(Timer);
	}
}

void USpiralPlatformGlow::RefreshCurrentSlice()
{
	// Called in the same damage batch, even between timer steps or while paused.
	RenderSlice(CurrentSlice, true);
}

void USpiralPlatformGlow::RenderSlice(int32 Slice, bool bForce)
{
	if (!Surface || !BuildSlice || (!bForce && Slice == CurrentSlice)) { return; }
	CurrentSlice = Slice;
	if (Slice == INDEX_NONE) { Surface->SetVisibility(false); return; }
	FDynamicMesh3 Mesh;
	Mesh.EnableVertexNormals(FVector3f::UnitZ());
	Mesh.EnableAttributes();
	Mesh.Attributes()->SetNumUVLayers(1);
	Mesh.Attributes()->EnableMaterialID();
	BuildSlice(Mesh, Slice);
	// Keep the source's exact clipping/UVs. A tiny normal offset prevents z fighting.
	for (int32 Vertex : Mesh.VertexIndicesItr())
	{
		Mesh.SetVertex(Vertex, Mesh.GetVertex(Vertex) + FVector3d(Mesh.GetVertexNormal(Vertex)) * .1);
	}
	for (int32 Triangle : Mesh.TriangleIndicesItr()) { Mesh.Attributes()->GetMaterialID()->SetValue(Triangle, 0); }
	const bool bVisible = Mesh.TriangleCount() > 0;
	// UE 5.8 can keep stale proxy buffers after a wholesale mesh replacement.
	// Re-register only this small effect, never the solid platform or its collision.
	Surface->UnregisterComponent();
	auto* Replacement = NewObject<UDynamicMesh>(Surface);
	Replacement->SetMesh(MoveTemp(Mesh));
	Surface->SetDynamicMesh(Replacement);
	Surface->RegisterComponent();
	Surface->NotifyMeshUpdated();
	Surface->SetVisibility(bVisible);
}

void USpiralPlatformGlow::Release()
{
	if (Owner.IsValid() && Owner->GetWorld()) { Owner->GetWorld()->GetTimerManager().ClearTimer(Timer); }
	if (Surface)
	{
		if (Owner.IsValid()) { Owner->RemoveInstanceComponent(Surface); }
		Surface->DestroyComponent();
		Surface = nullptr;
	}
	Owner.Reset();
	BuildSlice = nullptr;
	CurrentSlice = INDEX_NONE;
	SliceCount = 0;
}
