// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshActor.h"
#include "SimpleHouseActor2.generated.h"

class UMaterialInterface;

/** Horizontal placement of a floor's complete window group. */
UENUM(BlueprintType)
enum class ESimpleHouse2WindowAlignment : uint8
{
	Left UMETA(DisplayName = "Left"),
	Center UMETA(DisplayName = "Center"),
	Right UMETA(DisplayName = "Right")
};

/** Overall size and storey layout of the generated house. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse2BuildingSettings
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

	/** Thickness of the walls, roof and optional ground slab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", AdvancedDisplay, meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float WallThickness = 25.0f;
};

/**
 * Window settings for one storey. Array element zero represents the ground
 * storey, and the following elements continue from bottom to top.
 */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse2FloorWindowSettings
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
	ESimpleHouse2WindowAlignment Alignment = ESimpleHouse2WindowAlignment::Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float WindowSpacing = 60.0f;
};

/** Door settings for the ground storey's front facade. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse2DoorSettings
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
 * A multi-storey simple house generated entirely with Dynamic Mesh and
 * Geometry Script. Door and window openings are real boolean-cut openings.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API ASimpleHouseActor2 : public ADynamicMeshActor
{
	GENERATED_BODY()

public:
	ASimpleHouseActor2(const FObjectInitializer& ObjectInitializer);

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 2|Building")
	FSimpleHouse2BuildingSettings Building;

	/**
	 * One entry per storey, ordered from bottom to top. The array is
	 * automatically resized to match Building.FloorCount.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 2|Windows", meta = (TitleProperty = "FloorNumber"))
	TArray<FSimpleHouse2FloorWindowSettings> FloorWindows;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 2|Door")
	FSimpleHouse2DoorSettings Door;

	/** Adds a solid slab at the bottom of the house. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 2|Ground")
	bool bHasGroundFloor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 2|Rendering")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 2|Collision")
	bool bEnableCollision = true;

	/** Recreates the complete house using the current editor parameters. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Simple House 2")
	void RebuildMesh();

private:
	void SynchronizeFloorWindowSettings();
};
