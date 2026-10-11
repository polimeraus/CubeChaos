#include "QuadPanelConeStructure.h"
#include "ChunkedDestructibleConeStructure.h"
#include "DynamicMesh/DynamicMesh3.h"

void AQuadPanelConeStructure::PlaySpiralPlatformGlow()
{
	StopSpiralPlatformGlow();
	if (!SpiralPlatformGlow.bEnabled || !bGeneratePlatforms ||
		PlatformStyle != EConeStructurePlatformStyle::RisingSpiral || GridHeightBlocks <= 0) { return; }
	SpiralPlatformGlowRuntime = NewObject<USpiralPlatformGlow>(this);
	SpiralPlatformGlowRuntime->Start(this, SpiralPlatformGlow, FlashMaterial, GetSpiralSegmentCount() * 2,
		GetSpiralBottomHeight(true) >= GetSpiralBottomHeight(false),
		[this](UE::Geometry::FDynamicMesh3& Mesh, int32 Slice) { AppendSpiralSurfaces(Mesh, INDEX_NONE, Slice); });
}

void AQuadPanelConeStructure::StopSpiralPlatformGlow()
{
	if (SpiralPlatformGlowRuntime) { SpiralPlatformGlowRuntime->Release(); SpiralPlatformGlowRuntime = nullptr; }
}

void AQuadPanelConeStructure::SetSpiralPlatformGlowSettings(FSpiralPlatformGlowSettings NewSettings)
{
	SpiralPlatformGlow = NewSettings;
	PlaySpiralPlatformGlow();
}

void AQuadPanelConeStructure::RefreshSpiralPlatformGlowAfterGeneration()
{
	if (HasActorBegunPlay()) { PlaySpiralPlatformGlow(); }
}

void AChunkedDestructibleConeStructure::PlaySpiralPlatformGlow()
{
	StopSpiralPlatformGlow();
	if (!SpiralPlatformGlow.bEnabled || !bGeneratePlatforms ||
		PlatformStyle != EConeStructurePlatformStyle::RisingSpiral || GridHeightBlocks <= 0) { return; }
	SpiralPlatformGlowRuntime = NewObject<USpiralPlatformGlow>(this);
	SpiralPlatformGlowRuntime->Start(this, SpiralPlatformGlow, FlashMaterial, GetSpiralSegmentCount() * 2,
		GetSpiralBottomHeight(true) >= GetSpiralBottomHeight(false),
		[this](UE::Geometry::FDynamicMesh3& Mesh, int32 Slice) { AppendSpiralSurfaces(Mesh, INDEX_NONE, Slice); });
}

void AChunkedDestructibleConeStructure::StopSpiralPlatformGlow()
{
	if (SpiralPlatformGlowRuntime) { SpiralPlatformGlowRuntime->Release(); SpiralPlatformGlowRuntime = nullptr; }
}

void AChunkedDestructibleConeStructure::SetSpiralPlatformGlowSettings(FSpiralPlatformGlowSettings NewSettings)
{
	SpiralPlatformGlow = NewSettings;
	PlaySpiralPlatformGlow();
}

void AChunkedDestructibleConeStructure::RefreshSpiralPlatformGlowAfterGeneration()
{
	if (HasActorBegunPlay()) { PlaySpiralPlatformGlow(); }
}

