// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "GameFramework/Actor.h"
#include "SimpleHouseActor8.generated.h"

class UMaterialInterface;
class USceneComponent;

/** Horizontal placement of a floor's complete window group. */
UENUM(BlueprintType)
enum class ESimpleHouse8WindowAlignment : uint8
{
	Left UMETA(DisplayName = "Left"),
	Center UMETA(DisplayName = "Center"),
	Right UMETA(DisplayName = "Right")
};

/** Shape of the boolean opening generated for a floor's windows. */
UENUM(BlueprintType)
enum class ESimpleHouse8WindowType : uint8
{
	Rectangular UMETA(DisplayName = "Rectangular"),
	Arched UMETA(DisplayName = "Arched / Oval Top")
};

/** Shape of the generated roof. */
UENUM(BlueprintType)
enum class ESimpleHouse8RoofType : uint8
{
	Gable UMETA(DisplayName = "Gable"),
	Pyramid UMETA(DisplayName = "Pyramid")
};

/** Area covered by the generated ground slab. */
UENUM(BlueprintType)
enum class ESimpleHouse8GroundCoverage : uint8
{
	InteriorOnly UMETA(DisplayName = "Interior Only"),
	FullBuildingFootprint UMETA(DisplayName = "Full Building Footprint (Including Walls)")
};

/** Individually addressable pieces of the generated house. */
UENUM(BlueprintType)
enum class ESimpleHouse8Part : uint8
{
	Roof UMETA(DisplayName = "Roof"),
	Ceiling UMETA(DisplayName = "Ceiling"),
	Ground UMETA(DisplayName = "Ground"),
	FrontWall UMETA(DisplayName = "Front Wall"),
	BackWall UMETA(DisplayName = "Back Wall"),
	LeftWall UMETA(DisplayName = "Side Wall 1"),
	RightWall UMETA(DisplayName = "Side Wall 2")
};

/** Overall size and storey layout of the generated house. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse8BuildingSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float BaseWidth = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float BaseDepth = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "1", ClampMax = "100", UIMin = "1", UIMax = "20"))
	int32 FloorCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float FloorHeight = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", AdvancedDisplay, meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float WallThickness = 25.0f;
};

/** Selectable roof shape and optional flat ceiling settings. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse8RoofSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof")
	ESimpleHouse8RoofType RoofType = ESimpleHouse8RoofType::Gable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (DisplayName = "Gable Height", EditCondition = "RoofType == ESimpleHouse8RoofType::Gable", EditConditionHides, ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float RoofHeight = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (EditCondition = "RoofType == ESimpleHouse8RoofType::Pyramid", EditConditionHides, ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float PyramidHeight = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof")
	bool bHasCeiling = false;
};

/** Window settings for one storey, ordered from the bottom upwards. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse8FloorWindowSettings
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor")
	int32 FloorNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows")
	bool bHasWindows = true;

	/** All windows on this storey use the selected opening shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows"))
	ESimpleHouse8WindowType WindowType = ESimpleHouse8WindowType::Rectangular;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "0", ClampMax = "100", UIMin = "0", UIMax = "20"))
	int32 WindowCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float WindowWidth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float WindowHeight = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows"))
	ESimpleHouse8WindowAlignment Alignment = ESimpleHouse8WindowAlignment::Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float WindowSpacing = 60.0f;
};

/** Door settings for the ground storey of one facade. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse8DoorSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	bool bHasDoor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float DoorWidth = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float DoorHeight = 220.0f;
};

/** Independent door and per-storey window settings for one facade. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse8FacadeSettings
{
	GENERATED_BODY()

	/** Enables door and window generation on this facade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facade")
	bool bEnableOpenings = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facade", meta = (EditCondition = "bEnableOpenings"))
	FSimpleHouse8DoorSettings Door;

	/**
	 * One entry per storey, ordered from bottom to top. The array is
	 * automatically resized to match the building's floor count.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facade", meta = (EditCondition = "bEnableOpenings", TitleProperty = "FloorNumber"))
	TArray<FSimpleHouse8FloorWindowSettings> FloorWindows;
};

/** Editor and runtime visibility state for every independently generated part. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse8PartVisibility
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parts")
	bool bRoofVisible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parts")
	bool bCeilingVisible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parts")
	bool bGroundVisible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parts")
	bool bFrontWallVisible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parts")
	bool bBackWallVisible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parts")
	bool bSideWall1Visible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parts")
	bool bSideWall2Visible = true;
};

/**
 * A component-based Dynamic Mesh house. Every architectural piece is a
 * separate UDynamicMeshComponent and can be accessed or hidden independently.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API ASimpleHouseActor8 : public AActor
{
	GENERATED_BODY()

public:
	ASimpleHouseActor8(const FObjectInitializer& ObjectInitializer);

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Building")
	FSimpleHouse8BuildingSettings Building;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Roof")
	FSimpleHouse8RoofSettings Roof;

	/** Enabled by default, matching SimpleHouseActor5 behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Openings", meta = (DisplayName = "Front Face"))
	FSimpleHouse8FacadeSettings FrontFacade;

	/** Disabled by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Openings", meta = (DisplayName = "Back Face"))
	FSimpleHouse8FacadeSettings BackFacade;

	/** Disabled by default. Its horizontal direction follows local Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Openings", meta = (DisplayName = "Side Wall 1"))
	FSimpleHouse8FacadeSettings SideWall1Facade;

	/** Disabled by default. Its horizontal direction follows local Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Openings", meta = (DisplayName = "Side Wall 2"))
	FSimpleHouse8FacadeSettings SideWall2Facade;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Ground")
	bool bHasGroundFloor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Ground", meta = (EditCondition = "bHasGroundFloor", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float GroundThickness = 25.0f;

	/**
	 * Interior Only preserves SimpleHouseActor7's ground dimensions.
	 * Full Building Footprint also extends the slab beneath every wall.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Ground", meta = (EditCondition = "bHasGroundFloor"))
	ESimpleHouse8GroundCoverage GroundCoverage = ESimpleHouse8GroundCoverage::FullBuildingFootprint;

	/**
	 * Changing these flags in the editor updates the corresponding component
	 * visibility during construction. At runtime use SetPartVisibility.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Parts")
	FSimpleHouse8PartVisibility PartVisibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Rendering")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 8|Collision")
	bool bEnableCollision = true;

	/** Root shared by all independently generated house parts. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 8|Parts")
	TObjectPtr<USceneComponent> HouseRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 8|Parts")
	TObjectPtr<UDynamicMeshComponent> RoofComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 8|Parts")
	TObjectPtr<UDynamicMeshComponent> CeilingComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 8|Parts")
	TObjectPtr<UDynamicMeshComponent> GroundComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 8|Parts")
	TObjectPtr<UDynamicMeshComponent> FrontWallComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 8|Parts")
	TObjectPtr<UDynamicMeshComponent> BackWallComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 8|Parts")
	TObjectPtr<UDynamicMeshComponent> SideWall1Component;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 8|Parts")
	TObjectPtr<UDynamicMeshComponent> SideWall2Component;

	/** Recreates every part as an independent Dynamic Mesh component. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Simple House 8")
	void RebuildMesh();

	/** Immediately reapplies all visibility flags without rebuilding geometry. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Simple House 8|Parts")
	void ApplyPartVisibility();

	/** Immediately shows or hides one part in the editor or at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Simple House 8|Parts")
	void SetPartVisibility(ESimpleHouse8Part Part, bool bVisible);

	/** Returns the Dynamic Mesh component belonging to the requested part. */
	UFUNCTION(BlueprintPure, Category = "Simple House 8|Parts")
	UDynamicMeshComponent* GetPartComponent(ESimpleHouse8Part Part) const;

private:
	void SynchronizeFloorWindowSettings();
	void FinalizeComponent(UDynamicMeshComponent* Component) const;
};
