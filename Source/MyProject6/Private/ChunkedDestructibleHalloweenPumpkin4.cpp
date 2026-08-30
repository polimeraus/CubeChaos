// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleHalloweenPumpkin4.h"

AChunkedDestructibleHalloweenPumpkin4::AChunkedDestructibleHalloweenPumpkin4()
{
    FloorPattern = EChunkedFloorPattern::Solid;
    ChunkSideLength = 64;
    bGenerateBottomFaces = true;
    bDestructibleByDefault = true;
    DefaultCellHealth = 25.0f;
    FlashColor = FLinearColor(1.0f, 0.18f, 0.01f, 1.0f);

    BodyRadiusBlocks = FVector2D(10.0f, 11.5f);
    BodyHalfHeightBlocks = 8.4f;
    VerticalProfilePower = 3.4f;
    RibCount = 6;
    RibDepth = 0.01f;
    ShellThicknessBlocks = 2.4f;
    BlockDimensions = FVector(30.0f, 30.0f, 30.0f);
    BlockSpacing = FVector(0.35f, 0.35f, 0.35f);
    bCarveJackOLanternFace = true;
    bEnableFaceGlowSurface = true;
    FaceGlowColor = FLinearColor(1.0f, 0.21f, 0.006f, 1.0f);
    FaceGlowIntensity = 4.0f;
    StemRadiusBlocks = 1.2f;
    StemHeightBlocks = 7.0f;
    StemLeanBlocks = FVector2D(0.2f, -2.5f);
    StemCurveBlocks = FVector2D(0.1f, -0.8f);
    bEnableInteriorLight = true;
    InteriorLightColor = FLinearColor(1.0f, 0.33f, 0.015f, 1.0f);
    InteriorLightIntensity = 1100.0f;
    InteriorLightAttenuationRadius = 1200.0f;
    BodyColor = FLinearColor(1.0f, 0.12f, 0.008f, 1.0f);
    RibColor = FLinearColor(0.43f, 0.025f, 0.003f, 1.0f);
    InteriorColor = FLinearColor(1.0f, 0.36f, 0.02f, 1.0f);
    StemColor = FLinearColor(0.11f, 0.045f, 0.012f, 1.0f);
    SoftEdgeBlend = 0.75f;
    EdgeRounding = 0.55f;
}

void AChunkedDestructibleHalloweenPumpkin4::ConfigureSoftEdgePumpkin(
    FVector2D NewBodyRadiusBlocks,
    float NewBodyHalfHeightBlocks,
    float NewShellThicknessBlocks,
    float NewStemHeightBlocks,
    FVector NewBlockDimensions,
    FVector NewBlockSpacing,
    bool bPreserveCellState)
{
    BodyRadiusBlocks = FVector2D(
        FMath::Clamp(FMath::Abs(NewBodyRadiusBlocks.X), 4.0, 32.0),
        FMath::Clamp(FMath::Abs(NewBodyRadiusBlocks.Y), 4.0, 32.0));
    BodyHalfHeightBlocks = FMath::Clamp(FMath::Abs(NewBodyHalfHeightBlocks), 4.0f, 32.0f);
    ShellThicknessBlocks = FMath::Clamp(FMath::Abs(NewShellThicknessBlocks), 1.0f, 8.0f);
    StemHeightBlocks = FMath::Clamp(FMath::Abs(NewStemHeightBlocks), 2.0f, 20.0f);
    BlockDimensions = FVector(
        FMath::Max(FMath::Abs(NewBlockDimensions.X), 1.0f),
        FMath::Max(FMath::Abs(NewBlockDimensions.Y), 1.0f),
        FMath::Max(FMath::Abs(NewBlockDimensions.Z), 1.0f));
    BlockSpacing = FVector(
        FMath::Max(NewBlockSpacing.X, 0.0f),
        FMath::Max(NewBlockSpacing.Y, 0.0f),
        FMath::Max(NewBlockSpacing.Z, 0.0f));

    VerticalProfilePower = FMath::Clamp(3.0f + SoftEdgeBlend * 1.5f, 2.0f, 6.0f);
    RibCount = FMath::Clamp(FMath::RoundToInt(8.0f - EdgeRounding * 4.0f), 4, 12);
    RibDepth = FMath::Clamp(0.025f + (1.0f - EdgeRounding) * 0.04f, 0.0f, 0.12f);
    RebuildPumpkin2(bPreserveCellState);
}

void AChunkedDestructibleHalloweenPumpkin4::SetSoftEdgePumpkinFaceEnabled(
    bool bEnabled,
    bool bPreserveCellState)
{
    SetPumpkin2FaceEnabled(bEnabled, bPreserveCellState);
}
