#include "DestructiblePlanarAnimation.h"
#include "ChunkedDestructibleFloor.h"
#include "ParametricPlatform.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RHI.h"

using namespace UE::Geometry;

namespace
{
	float GetEqualizerTransparency(const AChunkedDestructibleFloor* Owner)
	{
		const auto* Platform = Cast<AParametricPlatform>(Owner);
		if (!Platform) return 0.f;
		return FMath::IsFinite(Platform->EqualizerTransparency)
			? FMath::Clamp(Platform->EqualizerTransparency, 0.f, 1.f) : 0.8f;
	}
}

double FDestructiblePlanarFlow::TravelDistance(FVector2D Size) const
{
	if (Mode == EPlanarFlowMode::Radial)
	{
		return (Size * FVector2D(FMath::Max(Origin.X, 1.0 - Origin.X),
			FMath::Max(Origin.Y, 1.0 - Origin.Y))).Size();
	}
	const double Angle = FMath::DegreesToRadians(static_cast<double>(AngleDegrees));
	return FMath::Abs(FMath::Cos(Angle)) * Size.X + FMath::Abs(FMath::Sin(Angle)) * Size.Y;
}

FVector2D FDestructiblePlanarFlow::Coordinates(FVector2D Position, FVector2D Size) const
{
	const double Angle = FMath::DegreesToRadians(static_cast<double>(AngleDegrees));
	const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
	FVector2D Result;
	if (Mode == EPlanarFlowMode::Radial)
	{
		const FVector2D Delta = (Position - Origin) * Size;
		Result.X = Delta.Size() / FMath::Max(TravelDistance(Size), 1.0);
		const double Turns = (FMath::Atan2(Delta.Y, Delta.X) - Angle) / UE_TWO_PI;
		Result.Y = Turns - FMath::FloorToDouble(Turns);
	}
	else
	{
		const FVector2D Delta = (Position - FVector2D(0.5, 0.5)) * Size;
		const double CrossExtent = FMath::Abs(Direction.Y) * Size.X + FMath::Abs(Direction.X) * Size.Y;
		Result.X = 0.5 + FVector2D::DotProduct(Delta, Direction) / FMath::Max(TravelDistance(Size), 1.0);
		Result.Y = 0.5 + FVector2D::DotProduct(Delta, FVector2D(-Direction.Y, Direction.X)) / FMath::Max(CrossExtent, 1.0);
	}
	Result.X = FMath::Clamp(bReverse ? 1.0 - Result.X : Result.X, 0.0, 1.0);
	Result.Y = FMath::Clamp(Result.Y, 0.0, 1.0);
	return Result;
}

bool UDestructiblePlanarAnimation::Initialize(AChunkedDestructibleFloor* InOwner)
{
	Owner = InOwner;
	if (!Material)
	{
		UMaterialInterface* Parent = Owner->PlanarAnimationMaterial.LoadSynchronous();
		// Previously saved platform overrides may still name the shared stock material.
		// Upgrade that stock reference at runtime; explicitly authored custom materials remain valid.
		if (Owner->IsA<AParametricPlatform>() && Parent && Parent->GetPathName() ==
			TEXT("/Game/Materials/Structures/M_PlanarAnimation.M_PlanarAnimation"))
		{
			Parent = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/Materials/Structures/M_PlatformEqualizerBacklight.M_PlatformEqualizerBacklight"));
		}
		if (!Parent || Parent->GetBlendMode() != BLEND_Additive)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: planar animation requires the additive planar surface material."), *Owner->GetName());
			return false;
		}
		Material = UMaterialInstanceDynamic::Create(Parent, this);
		// A defined black input also keeps fixed-only animations from allocating an updateable texture.
		Material->SetTextureParameterValue(TEXT("MusicMask"), LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black")));
		Material->SetScalarParameterValue(TEXT("MusicEnabled"), 0.0f);
		Surfaces.SetNum(Owner->ChunkComponents.Num());
		for (int32 Index = 0; Index < Surfaces.Num(); ++Index) { RefreshChunk(Index); }
	}
	const double Angle = FMath::DegreesToRadians(static_cast<double>(Owner->PlanarFlow.AngleDegrees));
	Material->SetScalarParameterValue(TEXT("EqualizerTransparency"), GetEqualizerTransparency(Owner));
	Material->SetVectorParameterValue(TEXT("Flow"), FLinearColor(FMath::Cos(Angle), FMath::Sin(Angle),
		Owner->PlanarFlow.Mode == EPlanarFlowMode::Radial ? 1.0f : 0.0f, Owner->PlanarFlow.bReverse ? 1.0f : 0.0f));
	Material->SetVectorParameterValue(TEXT("Footprint"), FLinearColor(Owner->EffectiveGridSize.X,
		Owner->EffectiveGridSize.Y, Owner->PlanarFlow.Origin.X, Owner->PlanarFlow.Origin.Y));
	return true;
}

void UDestructiblePlanarAnimation::RefreshChunk(int32 Index)
{
	if (!Owner || !Material || !Owner->ChunkComponents.IsValidIndex(Index)) { return; }
	UDynamicMeshComponent* Source = Owner->ChunkComponents[Index];
	if (!Source || !Source->GetMesh()) { return; }
	if (!Surfaces.IsValidIndex(Index)) { Surfaces.SetNum(Owner->ChunkComponents.Num()); }
	UDynamicMeshComponent* Surface = Surfaces[Index];
	if (!Surface)
	{
		Surface = NewObject<UDynamicMeshComponent>(Owner, NAME_None, RF_Transient);
		Surfaces[Index] = Surface;
		Owner->AddInstanceComponent(Surface);
		Surface->SetupAttachment(Owner->FloorRoot);
		Surface->SetMobility(EComponentMobility::Movable);
		Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Surface->SetGenerateOverlapEvents(false);
		Surface->SetCanEverAffectNavigation(false);
		Surface->SetCastShadow(false);
		Surface->SetReceivesDecals(false);
		Surface->SetMaterial(0, Material);
		Surface->ComponentTags.Add(TEXT("PlanarAnimationSurface"));
	}
	const bool bRegistered = Surface->IsRegistered();
	if (bRegistered) { Surface->UnregisterComponent(); }
	// Copy the already clipped/merged surface, including motifs and destruction holes. No offsets.
	FDynamicMesh3 Mesh(*Source->GetMesh());
	Mesh.EnableAttributes();
	Mesh.Attributes()->SetNumUVLayers(FMath::Max(Mesh.Attributes()->NumUVLayers(), 2));
	FDynamicMeshUVOverlay* UV = Mesh.Attributes()->GetUVLayer(1);
	UV->ClearElements();
	const FVector2D Size(FMath::Max(Owner->EffectiveGridSize.X, 1.0), FMath::Max(Owner->EffectiveGridSize.Y, 1.0));
	for (const int32 Triangle : Mesh.TriangleIndicesItr())
	{
		const FIndex3i Vertices = Mesh.GetTriangle(Triangle);
		FIndex3i Elements;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d Position = Mesh.GetVertex(Vertices[Corner]);
			const FVector2D Normalized = (FVector2D(Position.X, Position.Y) - Owner->GridLocalMin) / Size;
			Elements[Corner] = UV->AppendElement(FVector2f(Normalized));
		}
		UV->SetTriangle(Triangle, Elements);
		// One material batch for the effect; the original chunk keeps its entire palette.
		if (Mesh.Attributes()->HasMaterialID()) { Mesh.Attributes()->GetMaterialID()->SetValue(Triangle, 0); }
	}
	UDynamicMesh* Replacement = NewObject<UDynamicMesh>(Surface);
	Replacement->SetMesh(MoveTemp(Mesh));
	Surface->SetDynamicMesh(Replacement);
	Surface->SetVisibility((bFixedVisible || bMusicVisible) && Source->GetMesh()->TriangleCount() > 0);
	Surface->RegisterComponent();
}

void UDestructiblePlanarAnimation::SetFixedAnimation(TConstArrayView<float> Alphas)
{
	if (!Material) { return; }
	const int32 Count = FMath::Min(Alphas.Num(), 32);
	bFixedVisible = Count > 0;
	const double Pitch = FMath::Max(1.0, FMath::Min(Owner->EffectiveCellDimensions.X + Owner->CellSpacing.X,
		Owner->EffectiveCellDimensions.Y + Owner->CellSpacing.Y));
	const float Band = FMath::Clamp(Owner->GlowWaveBandHeightBlocks * Pitch /
		FMath::Max(Owner->PlanarFlow.TravelDistance(Owner->EffectiveGridSize), 1.0), 0.001, 1.0);
	Material->SetVectorParameterValue(TEXT("Fixed"), FLinearColor(static_cast<float>(Owner->PlanarAnimationType),
		Count, Band, FMath::Clamp(Owner->SineRowsSimultaneousRowCount, 2, 32)));
	Material->SetVectorParameterValue(TEXT("WaveColor"), Owner->GlowWaveColor * FMath::Max(Owner->GlowWaveIntensity, 0.0f));
	Material->SetVectorParameterValue(TEXT("Spiral"), FLinearColor(Owner->SpiralGlowTurns,
		Owner->SpiralGlowStripeWidthDegrees / 360.0f, Owner->SpiralGlowStrandCount, Owner->SpiralGlowStartAngleDegrees / 360.0f));
	Material->SetScalarParameterValue(TEXT("SpiralDirection"), Owner->bReverseSpiralGlowDirection ? -1.0f : 1.0f);
	static const FName WaveParameters[] = {TEXT("Waves0"), TEXT("Waves1"), TEXT("Waves2"), TEXT("Waves3"),
		TEXT("Waves4"), TEXT("Waves5"), TEXT("Waves6"), TEXT("Waves7")};
	for (int32 Pack = 0; Pack < FMath::DivideAndRoundUp(Count, 4); ++Pack)
	{
		FLinearColor Values(-1, -1, -1, -1);
		for (int32 Channel = 0; Channel < 4 && Pack * 4 + Channel < Count; ++Channel) { Values.Component(Channel) = Alphas[Pack * 4 + Channel]; }
		Material->SetVectorParameterValue(WaveParameters[Pack], Values);
	}
	UpdateVisibility();
}

void UDestructiblePlanarAnimation::SetMusicMask(TConstArrayView<uint8> Mask, int32 Columns, int32 Rows)
{
	if (!Material || Columns < 1 || Rows < 1 || Mask.Num() != Columns * Rows) { return; }
	Material->SetScalarParameterValue(TEXT("EqualizerTransparency"), GetEqualizerTransparency(Owner));
	Material->SetVectorParameterValue(TEXT("Equalizer"), FLinearColor(Columns, Rows,
		static_cast<float>(Owner->MusicEqualizerBaseEdge), Owner->MusicAnimationType == EMusicAnimationType::Equalizer ? 1.0f : 0.0f));
	bMusicVisible = Mask.ContainsByPredicate([](uint8 Value) { return Value != 0; });
	if (bMusicVisible)
	{
		if (!MusicTexture || MusicTexture->GetSizeX() != Columns || MusicTexture->GetSizeY() != Rows)
		{
			MusicTexture = UTexture2D::CreateTransient(Columns, Rows, PF_G8);
			MusicTexture->SRGB = false;
			MusicTexture->NeverStream = true;
			MusicTexture->Filter = TF_Nearest;
			MusicTexture->AddressX = TA_Clamp;
			MusicTexture->AddressY = TA_Clamp;
			MusicTexture->UpdateResource();
			Material->SetTextureParameterValue(TEXT("MusicMask"), MusicTexture);
		}
		if (MusicTexture->GetResource())
		{
			// The render command owns its tiny immutable payload; actor teardown cannot invalidate it.
			uint8* Pixels = static_cast<uint8*>(FMemory::Malloc(Mask.Num()));
			FMemory::Memcpy(Pixels, Mask.GetData(), Mask.Num());
			auto* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Columns, Rows);
			MusicTexture->UpdateTextureRegions(0, 1, Region, Columns, 1, Pixels,
				[](uint8* Data, const FUpdateTextureRegion2D* UpdatedRegion) { FMemory::Free(Data); delete UpdatedRegion; });
		}
		Material->SetVectorParameterValue(TEXT("MusicColor"), Owner->MusicReactionColor *
			FMath::Max3(Owner->StrongMusicGlowIntensity, Owner->WeakMusicGlowIntensity, 0.0f));
	}
	Material->SetScalarParameterValue(TEXT("MusicEnabled"), bMusicVisible ? 1.0f : 0.0f);
	UpdateVisibility();
}

void UDestructiblePlanarAnimation::ClearMusic()
{
	bMusicVisible = false;
	if (Material) { Material->SetScalarParameterValue(TEXT("MusicEnabled"), 0.0f); }
	UpdateVisibility();
}

void UDestructiblePlanarAnimation::UpdateVisibility()
{
	const bool bVisible = bFixedVisible || bMusicVisible;
	if (bSurfacesVisible == bVisible) { return; }
	bSurfacesVisible = bVisible;
	for (UDynamicMeshComponent* Surface : Surfaces)
	{
		if (Surface) { Surface->SetVisibility(bVisible && Surface->GetMesh()->TriangleCount() > 0); }
	}
}

void UDestructiblePlanarAnimation::Release()
{
	for (UDynamicMeshComponent* Surface : Surfaces)
	{
		if (Surface) { Owner->RemoveInstanceComponent(Surface); Surface->DestroyComponent(); }
	}
	Surfaces.Empty();
	Material = nullptr;
	MusicTexture = nullptr;
	Owner = nullptr;
}
