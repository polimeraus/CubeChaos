// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleHalloweenPumpkin3.h"

AChunkedDestructibleHalloweenPumpkin3::AChunkedDestructibleHalloweenPumpkin3()
{
    FloorPattern = EChunkedFloorPattern::Solid;
    ChunkSideLength = 64;
    bGenerateBottomFaces = true;
    bDestructibleByDefault = true;
    DefaultCellHealth = 25.0f;
    FlashColor = FLinearColor(1.0f, 0.18f, 0.01f, 1.0f);

    BodyRadiusBlocks = FVector2D(9.5f, 11.0f);
    BodyHalfHeightBlocks = 8.0f;
    VerticalProfilePower = 2.6f;
    RibCount = 8;
    RibDepth = 0.03f;
    ShellThicknessBlocks = 2.1f;
    BlockDimensions = FVector(28.0f, 28.0f, 28.0f);
    BlockSpacing = FVector(0.5f, 0.5f, 0.5f);
    bCarveJackOLanternFace = true;
    bEnableFaceGlowSurface = true;
    FaceGlowColor = FLinearColor(1.0f, 0.22f, 0.004f, 1.0f);
    FaceGlowIntensity = 3.5f;
    StemRadiusBlocks = 1.35f;
    StemHeightBlocks = 7.5f;
    StemLeanBlocks = FVector2D(0.35f, -2.8f);
    StemCurveBlocks = FVector2D(0.2f, -1.1f);
    bEnableInteriorLight = true;
    InteriorLightColor = FLinearColor(1.0f, 0.30f, 0.01f, 1.0f);
    InteriorLightIntensity = 900.0f;
    InteriorLightAttenuationRadius = 1100.0f;
    BodyColor = FLinearColor(1.0f, 0.11f, 0.008f, 1.0f);
    RibColor = FLinearColor(0.42f, 0.02f, 0.002f, 1.0f);
    InteriorColor = FLinearColor(1.0f, 0.35f, 0.02f, 1.0f);
    StemColor = FLinearColor(0.12f, 0.05f, 0.015f, 1.0f);
}

void AChunkedDestructibleHalloweenPumpkin3::ConfigureSmoothHollowPumpkin(
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
    RebuildPumpkin2(bPreserveCellState);
}

void AChunkedDestructibleHalloweenPumpkin3::SetSmoothPumpkinFaceEnabled(
    bool bEnabled,
    bool bPreserveCellState)
{
    SetPumpkin2FaceEnabled(bEnabled, bPreserveCellState);
}
