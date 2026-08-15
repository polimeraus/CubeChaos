// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleHouse.h"
#include "SimpleHouseActorDestructable.generated.h"

/** Horizontal placement of a complete window group on one storey. */
UENUM(BlueprintType)
enum class ESimpleHouseDestructableWindowAlignment : uint8
{
	Left UMETA(DisplayName = "Left"),
	Center UMETA(DisplayName = "Center"),
	Right UMETA(DisplayName = "Right")
};

/** Opening silhouettes are rasterized onto the destructible cube grid. */
UENUM(BlueprintType)
enum class ESimpleHouseDestructableWindowType : uint8
{
	Rectangular UMETA(DisplayName = "Rectangular"),
	Arched UMETA(DisplayName = "Arched / Oval Top"),
	RoundedCorners UMETA(DisplayName = "Rounded Corners"),
	Circular UMETA(DisplayName = "Circular")
};

UENUM(BlueprintType)
enum class ESimpleHouseDestructableDoorType : uint8
{
	Rectangular UMETA(DisplayName = "Rectangular"),
	Arched UMETA(DisplayName = "Arched / Semicircular Top")
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouseDestructableBuildingSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", Units = "cm"))
	float BaseWidth = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", Units = "cm"))
	float BaseDepth = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "1", ClampMax = "100"))
	int32 FloorCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", Units = "cm"))
	float FloorHeight = 300.0f;

	/** Every logical destruction cell is a cube with this edge length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (DisplayName = "Destructible Cube Size", ClampMin = "5.0", Units = "cm"))
	float CubeSize = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (DisplayName = "Cube Spacing", ClampMin = "0.0", Units = "cm"))
	float CubeSpacing = 0.0f;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouseDestructableRoofSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (DisplayName = "Gable Height", ClampMin = "10.0", Units = "cm"))
	float RoofHeight = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (ClampMin = "0.0", Units = "cm"))
	float Overhang = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof")
	bool bHasCeiling = false;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouseDestructableFloorWindowSettings
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor")
	int32 FloorNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows")
	bool bHasWindows = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows"))
	ESimpleHouseDestructableWindowType WindowType = ESimpleHouseDestructableWindowType::Rectangular;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "0", ClampMax = "20"))
	int32 WindowCount = 2;

	/** Circular windows use this value as their diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "1.0", Units = "cm"))
	float WindowWidth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows && WindowType != ESimpleHouseDestructableWindowType::Circular", EditConditionHides, ClampMin = "1.0", Units = "cm"))
	float WindowHeight = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows && WindowType == ESimpleHouseDestructableWindowType::RoundedCorners", EditConditionHides, ClampMin = "0.0", ClampMax = "100.0"))
	float RoundedDegree = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows"))
	ESimpleHouseDestructableWindowAlignment Alignment = ESimpleHouseDestructableWindowAlignment::Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "0.0", Units = "cm"))
	float WindowSpacing = 60.0f;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouseDestructableDoorSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	bool bHasDoor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor"))
	ESimpleHouseDestructableDoorType DoorType = ESimpleHouseDestructableDoorType::Rectangular;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor", ClampMin = "1.0", Units = "cm"))
	float DoorWidth = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor", ClampMin = "1.0", Units = "cm"))
	float DoorHeight = 220.0f;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouseDestructableFacadeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facade")
	bool bEnableOpenings = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facade", meta = (EditCondition = "bEnableOpenings"))
	FSimpleHouseDestructableDoorSettings Door;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facade", meta = (EditCondition = "bEnableOpenings", TitleProperty = "FloorNumber"))
	TArray<FSimpleHouseDestructableFloorWindowSettings> FloorWindows;
};

/**
 * SimpleHouseActor19-inspired house assembled only from destructible logical cubes.
 * It deliberately uses the chunked destruction system, not Geometry Script.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Simple House Actor Destructable"))
class MYPROJECT6_API ASimpleHouseActorDestructable : public AChunkedDestructibleHouse
{
	GENERATED_BODY()

public:
	ASimpleHouseActorDestructable();

	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House Destructable|Building", meta = (ShowOnlyInnerProperties))
	FSimpleHouseDestructableBuildingSettings Building;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House Destructable|Roof", meta = (ShowOnlyInnerProperties))
	FSimpleHouseDestructableRoofSettings Roof;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House Destructable|Openings", meta = (DisplayName = "Front Face"))
	FSimpleHouseDestructableFacadeSettings FrontFacade;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House Destructable|Openings", meta = (DisplayName = "Back Face"))
	FSimpleHouseDestructableFacadeSettings BackFacade;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House Destructable|Openings", meta = (DisplayName = "Side Wall 1"))
	FSimpleHouseDestructableFacadeSettings SideWall1Facade;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House Destructable|Openings", meta = (DisplayName = "Side Wall 2"))
	FSimpleHouseDestructableFacadeSettings SideWall2Facade;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House Destructable|Ground")
	bool bHasGroundFloor = true;

	/** Applies these settings and rebuilds the logical cube/chunk data. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Simple House Destructable|Generation")
	void RebuildDestructableHouse(bool bPreserveCellState = false);

	/** Actual dimensions after the requested values have been snapped to the cube grid. */
	UFUNCTION(BlueprintPure, Category = "Simple House Destructable|Generation")
	FVector GetSnappedBuildingDimensions() const;

protected:
	virtual bool IsOpeningCell(int32 X, int32 Y, int32 Z) const override;
	virtual bool IsWallCell(int32 X, int32 Y, int32 Z) const override;
	virtual bool GetCellPolygonXZ(FIntPoint Cell, TArray<FVector2D>& OutPolygon) const override;

private:
	void SynchronizeFloorWindowSettings();
	void ApplySettingsToCubeGrid();
	bool FacadeHasOpeningAtCell(
		const FSimpleHouseDestructableFacadeSettings& Facade,
		int32 HorizontalCell,
		int32 HorizontalCellCount,
		int32 Z) const;
};
