// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshActor.h"
#include "SimpleHouseActor3.generated.h"

class UMaterialInterface;

/** Horizontal placement of a floor's complete window group. */
UENUM(BlueprintType)
enum class ESimpleHouse3WindowAlignment : uint8
{
	Left UMETA(DisplayName = "Left"),
	Center UMETA(DisplayName = "Center"),
	Right UMETA(DisplayName = "Right")
};

/** Overall size and storey layout of the generated house. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse3BuildingSettings
{
	GENERATED_BODY()

	/** Width of the rectangular footprint along local X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float BaseWidth = 600.0f;

	/** Depth of the rectangular footprint along local Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float BaseDepth = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "1", ClampMax = "100", UIMin = "1", UIMax = "20"))
	int32 FloorCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float FloorHeight = 300.0f;

	/** Thickness of the walls, roof, ceiling and optional ground slab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", AdvancedDisplay, meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float WallThickness = 25.0f;
};

/** Gable roof and optional flat ceiling settings. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse3RoofSettings
{
	GENERATED_BODY()

	/** Vertical distance from the top of the walls to the roof ridge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float RoofHeight = 250.0f;

	/**
	 * Adds a horizontal ceiling below the gable roof. When disabled, the
	 * interior remains open all the way up to the roof.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof")
	bool bHasCeiling = false;
};

/**
 * Window settings for one storey. Array element zero represents the ground
 * storey, and the following elements continue from bottom to top.
 */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse3FloorWindowSettings
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor")
	int32 FloorNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows")
	bool bHasWindows = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "0", ClampMax = "100", UIMin = "0", UIMax = "20"))
	int32 WindowCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float WindowWidth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float WindowHeight = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows"))
	ESimpleHouse3WindowAlignment Alignment = ESimpleHouse3WindowAlignment::Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float WindowSpacing = 60.0f;
};

/** Door settings for the ground storey's front facade. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse3DoorSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	bool bHasDoor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float DoorWidth = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float DoorHeight = 220.0f;
};

/**
 * A multi-storey Dynamic Mesh house with a gable roof. Door and window
 * openings are real boolean-cut openings.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API ASimpleHouseActor3 : public ADynamicMeshActor
{
	GENERATED_BODY()

public:
	ASimpleHouseActor3(const FObjectInitializer& ObjectInitializer);

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 3|Building")
	FSimpleHouse3BuildingSettings Building;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 3|Roof")
	FSimpleHouse3RoofSettings Roof;

	/**
	 * One entry per storey, ordered from bottom to top. The array is
	 * automatically resized to match Building.FloorCount.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 3|Windows", meta = (TitleProperty = "FloorNumber"))
	TArray<FSimpleHouse3FloorWindowSettings> FloorWindows;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 3|Door")
	FSimpleHouse3DoorSettings Door;

	/** Adds a solid slab at the bottom of the house. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 3|Ground")
	bool bHasGroundFloor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 3|Rendering")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 3|Collision")
	bool bEnableCollision = true;

	/** Recreates the complete house using the current editor parameters. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Simple House 3")
	void RebuildMesh();

private:
	void SynchronizeFloorWindowSettings();
};
