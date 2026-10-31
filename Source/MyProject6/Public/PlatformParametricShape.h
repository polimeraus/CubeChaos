#pragma once

#include "CoreMinimal.h"
#include "PlatformParametricShape.generated.h"

/** Smooth, variable-width arm ending in a circular platform. All widths are full widths. */
USTRUCT(BlueprintType)
struct FPlatformParametricArm
{
    GENERATED_BODY()

    /** Distance from the central circle's edge to the end platform's center. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(ClampMin="50", ClampMax="50000", Units="cm"))
    float Length = 1700;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(ClampMin="25", ClampMax="10000", Units="cm"))
    float EndRadius = 650;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(ClampMin="25", ClampMax="10000", Units="cm"))
    float RootWidth = 1000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(ClampMin="25", ClampMax="10000", Units="cm"))
    float NeckWidth = 380;

    /** Width at the end-circle center. Limited to the circle's diameter to retain its round end. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(ClampMin="25", ClampMax="10000", Units="cm"))
    float EndWidth = 1000;

    /** Position of the narrowest part along the curve: 0 = root, 1 = end. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(ClampMin="0.15", ClampMax="0.85"))
    float NeckPosition = .48f;

    /** Sideways bow, relative to arm length. Negative values bend the opposite way. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(ClampMin="-1", ClampMax="1"))
    float Bend = .22f;

    /** Opposing curvature in the two halves of the arm. Zero gives a single flowing bow. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(ClampMin="-1", ClampMax="1", DisplayName="S Curve"))
    float SCurve = 0;

    /** Moves the end circle sideways without changing the direction at the root. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(ClampMin="-30000", ClampMax="30000", Units="cm"))
    float EndOffset = 0;
};

/** Optional exception for an arm. Missing entries inherit Default Arm. */
USTRUCT(BlueprintType)
struct FPlatformParametricArmOverride
{
    GENERATED_BODY()

    /** Zero-based arm index. First entry wins if the same index appears twice. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(ClampMin="0", ClampMax="15"))
    int32 ArmIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(Units="deg", EditCondition="bEnabled"))
    float AngleOffset = 0;

    /** Off inherits Default Arm (including mirrored bends). On uses all values in Shape. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(EditCondition="bEnabled"))
    bool bOverrideShape = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arm", meta=(EditCondition="bEnabled && bOverrideShape", EditConditionHides))
    FPlatformParametricArm Shape;
};

USTRUCT(BlueprintType)
struct FPlatformParametricShape
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parametric Shape", meta=(ClampMin="50", ClampMax="20000", Units="cm"))
    float CenterRadius = 800;

    /** Zero produces just the center circle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parametric Shape", meta=(ClampMin="0", ClampMax="16"))
    int32 ArmCount = 3;

    /** Full circle: heading of arm 0. Partial spread: heading of the spread's middle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parametric Shape", meta=(Units="deg"))
    float RotationDegrees = -90;

    /** 360 distributes arms around the center; smaller values create a fan or branching path. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parametric Shape", meta=(ClampMin="0", ClampMax="360", Units="deg"))
    float SpreadDegrees = 360;

    /** Mirror the default bends about the layout heading; arms on the symmetry axis stay straight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parametric Shape")
    bool bMirrorBends = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parametric Shape", meta=(ShowOnlyInnerProperties))
    FPlatformParametricArm DefaultArm;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parametric Shape", meta=(TitleProperty="ArmIndex"))
    TArray<FPlatformParametricArmOverride> ArmOverrides;

    /** Softens the union of the center, arms and end circles. Zero gives a precise union. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parametric Shape", meta=(ClampMin="0", ClampMax="2000", Units="cm"))
    float JunctionBlend = 220;

    /** Uniform scale preserves round centers and end circles. Shape Size is calculated automatically. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parametric Shape", meta=(ClampMin="0.05", ClampMax="20"))
    float UniformScale = 1;

    /** Samples across the longest dimension; thin arms automatically request more, up to 512. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Parametric Shape", meta=(ClampMin="96", ClampMax="512"))
    int32 OutlineResolution = 256;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Parametric Shape", meta=(ClampMin="16", ClampMax="128"))
    int32 CurveSamples = 48;
};
