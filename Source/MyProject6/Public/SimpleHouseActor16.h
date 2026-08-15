// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "GameFramework/Actor.h"
#include "SimpleHouseActor16.generated.h"

class UMaterialInterface;
class USceneComponent;

/** Horizontal placement of a floor's complete window group. */
UENUM(BlueprintType)
enum class ESimpleHouse16WindowAlignment : uint8
{
	Left UMETA(DisplayName = "Left"),
	Center UMETA(DisplayName = "Center"),
	Right UMETA(DisplayName = "Right")
};

/** Shape of the boolean opening generated for a floor's windows. */
UENUM(BlueprintType)
enum class ESimpleHouse16WindowType : uint8
{
	Rectangular UMETA(DisplayName = "Rectangular"),
	Arched UMETA(DisplayName = "Arched / Oval Top"),
	RoundedCorners UMETA(DisplayName = "Rounded Corners"),
	Circular UMETA(DisplayName = "Circular")
};

/** Visual treatment generated around each window opening. */
UENUM(BlueprintType)
enum class ESimpleHouse16WindowBorderStyle : uint8
{
	ParametricFrame UMETA(DisplayName = "Parametric Frame"),
	PaintSplatter UMETA(DisplayName = "Paint Splatter")
};

/** Shape of the door opening generated on a facade. */
UENUM(BlueprintType)
enum class ESimpleHouse16DoorType : uint8
{
	Rectangular UMETA(DisplayName = "Rectangular"),
	Arched UMETA(DisplayName = "Arched / Semicircular Top")
};

/** Shape of the generated roof. */
UENUM(BlueprintType)
enum class ESimpleHouse16RoofType : uint8
{
	Gable UMETA(DisplayName = "Gable"),
	Pyramid UMETA(DisplayName = "Pyramid")
};

/** Area covered by the generated ground slab. */
UENUM(BlueprintType)
enum class ESimpleHouse16GroundCoverage : uint8
{
	InteriorOnly UMETA(DisplayName = "Interior Only"),
	FullBuildingFootprint UMETA(DisplayName = "Full Building Footprint (Including Walls)")
};

/** Individually addressable pieces of the generated house. */
UENUM(BlueprintType)
enum class ESimpleHouse16Part : uint8
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
struct MYPROJECT6_API FSimpleHouse16BuildingSettings
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
struct MYPROJECT6_API FSimpleHouse16RoofSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof")
	ESimpleHouse16RoofType RoofType = ESimpleHouse16RoofType::Gable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (DisplayName = "Gable Height", EditCondition = "RoofType == ESimpleHouse16RoofType::Gable", EditConditionHides, ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float RoofHeight = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (EditCondition = "RoofType == ESimpleHouse16RoofType::Pyramid", EditConditionHides, ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float PyramidHeight = 250.0f;

	/** Extends the roof beyond the front and back wall planes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (DisplayName = "Front / Back Overhang", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float FrontBackOverhang = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof")
	bool bHasCeiling = false;
};

/** Window settings for one storey, ordered from the bottom upwards. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse16FloorWindowSettings
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor")
	int32 FloorNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows")
	bool bHasWindows = true;

	/** All windows on this storey use the selected opening shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows"))
	ESimpleHouse16WindowType WindowType = ESimpleHouse16WindowType::Rectangular;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "0", ClampMax = "100", UIMin = "0", UIMax = "20"))
	int32 WindowCount = 2;

	/** For Circular windows this value is used as the diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float WindowWidth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows && WindowType != ESimpleHouse16WindowType::Circular", EditConditionHides, ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float WindowHeight = 120.0f;

	/**
	 * Controls the corner roundness for Rounded Corners windows.
	 * Zero keeps square corners; 100 uses the largest valid corner radius.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (DisplayName = "Rounded Degree", EditCondition = "bHasWindows && WindowType == ESimpleHouse16WindowType::RoundedCorners", EditConditionHides, ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0"))
	float RoundedDegree = 20.0f;

	/**
	 * Parametric Frame preserves the solid frame from Actor14. Paint Splatter
	 * places the selected Window Object Material directly on the wall surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows"))
	ESimpleHouse16WindowBorderStyle WindowBorderStyle =
		ESimpleHouse16WindowBorderStyle::ParametricFrame;

	/** Average distance that the paint extends beyond the window opening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (DisplayName = "Paint Spread", EditCondition = "bHasWindows && WindowBorderStyle == ESimpleHouse16WindowBorderStyle::PaintSplatter", EditConditionHides, ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float PaintSpread = 50.0f;

	/** Controls edge variation, spikes, drips, and the number of droplets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (DisplayName = "Splatter Amount", EditCondition = "bHasWindows && WindowBorderStyle == ESimpleHouse16WindowBorderStyle::PaintSplatter", EditConditionHides, ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0"))
	float PaintSplatterAmount = 85.0f;

	/** Changes the deterministic paint pattern without changing its settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (DisplayName = "Paint Seed", EditCondition = "bHasWindows && WindowBorderStyle == ESimpleHouse16WindowBorderStyle::PaintSplatter", EditConditionHides))
	int32 PaintSplatterSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows"))
	ESimpleHouse16WindowAlignment Alignment = ESimpleHouse16WindowAlignment::Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float WindowSpacing = 60.0f;

	/**
	 * Optional material overrides for the separate window frame or paint
	 * objects on this floor. Entries follow the windows from left to right.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Windows", meta = (EditCondition = "bHasWindows", DisplayName = "Window Object Materials (Left to Right)"))
	TArray<TObjectPtr<UMaterialInterface>> WindowObjectMaterials;
};

/** Door settings for the ground storey of one facade. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse16DoorSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	bool bHasDoor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor"))
	ESimpleHouse16DoorType DoorType = ESimpleHouse16DoorType::Rectangular;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float DoorWidth = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float DoorHeight = 220.0f;

	/** Optional material override for this facade's separate door object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (EditCondition = "bHasDoor"))
	TObjectPtr<UMaterialInterface> DoorObjectMaterial = nullptr;
};

/** Independent door and per-storey window settings for one facade. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse16FacadeSettings
{
	GENERATED_BODY()

	/** Enables door and window generation on this facade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facade")
	bool bEnableOpenings = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facade", meta = (EditCondition = "bEnableOpenings"))
	FSimpleHouse16DoorSettings Door;

	/**
	 * One entry per storey, ordered from bottom to top. The array is
	 * automatically resized to match the building's floor count.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facade", meta = (EditCondition = "bEnableOpenings", TitleProperty = "FloorNumber"))
	TArray<FSimpleHouse16FloorWindowSettings> FloorWindows;
};

/** Editor and runtime visibility state for every independently generated part. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FSimpleHouse16PartVisibility
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
class MYPROJECT6_API ASimpleHouseActor16 : public AActor
{
	GENERATED_BODY()

public:
	ASimpleHouseActor16(const FObjectInitializer& ObjectInitializer);

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Building")
	FSimpleHouse16BuildingSettings Building;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Roof")
	FSimpleHouse16RoofSettings Roof;

	/** Enabled by default, matching SimpleHouseActor5 behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Openings", meta = (DisplayName = "Front Face"))
	FSimpleHouse16FacadeSettings FrontFacade;

	/** Disabled by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Openings", meta = (DisplayName = "Back Face"))
	FSimpleHouse16FacadeSettings BackFacade;

	/** Disabled by default. Its horizontal direction follows local Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Openings", meta = (DisplayName = "Side Wall 1"))
	FSimpleHouse16FacadeSettings SideWall1Facade;

	/** Disabled by default. Its horizontal direction follows local Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Openings", meta = (DisplayName = "Side Wall 2"))
	FSimpleHouse16FacadeSettings SideWall2Facade;

	/**
	 * Amount of wall retained around every separate window and door object.
	 * It is applied on both sides of a window opening.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Opening Objects", meta = (ClampMin = "0.1", UIMin = "0.1", Units = "cm"))
	float OpeningObjectBorder = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Ground")
	bool bHasGroundFloor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Ground", meta = (EditCondition = "bHasGroundFloor", ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float GroundThickness = 1.0f;

	/**
	 * Interior Only preserves SimpleHouseActor7's ground dimensions.
	 * Full Building Footprint also extends the slab beneath every wall.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Ground", meta = (EditCondition = "bHasGroundFloor"))
	ESimpleHouse16GroundCoverage GroundCoverage = ESimpleHouse16GroundCoverage::FullBuildingFootprint;

	/**
	 * Changing these flags in the editor updates the corresponding component
	 * visibility during construction. At runtime use SetPartVisibility.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Parts")
	FSimpleHouse16PartVisibility PartVisibility;

	/** Used by every part that does not have a material override below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Rendering", meta = (DisplayName = "Default Material"))
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Rendering|Part Materials")
	TObjectPtr<UMaterialInterface> RoofMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Rendering|Part Materials")
	TObjectPtr<UMaterialInterface> CeilingMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Rendering|Part Materials")
	TObjectPtr<UMaterialInterface> GroundMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Rendering|Part Materials")
	TObjectPtr<UMaterialInterface> FrontWallMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Rendering|Part Materials")
	TObjectPtr<UMaterialInterface> BackWallMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Rendering|Part Materials")
	TObjectPtr<UMaterialInterface> SideWall1Material = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Rendering|Part Materials")
	TObjectPtr<UMaterialInterface> SideWall2Material = nullptr;

	/** Material for the visible wall-thickness surfaces inside window openings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Rendering|Opening Materials")
	TObjectPtr<UMaterialInterface> WindowRevealMaterial = nullptr;

	/** Material for the visible wall-thickness surfaces inside door openings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Rendering|Opening Materials")
	TObjectPtr<UMaterialInterface> DoorRevealMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House 16|Collision")
	bool bEnableCollision = true;

	/** Root shared by all independently generated house parts. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 16|Parts")
	TObjectPtr<USceneComponent> HouseRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 16|Parts")
	TObjectPtr<UDynamicMeshComponent> RoofComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 16|Parts")
	TObjectPtr<UDynamicMeshComponent> CeilingComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 16|Parts")
	TObjectPtr<UDynamicMeshComponent> GroundComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 16|Parts")
	TObjectPtr<UDynamicMeshComponent> FrontWallComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 16|Parts")
	TObjectPtr<UDynamicMeshComponent> BackWallComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 16|Parts")
	TObjectPtr<UDynamicMeshComponent> SideWall1Component;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simple House 16|Parts")
	TObjectPtr<UDynamicMeshComponent> SideWall2Component;

	/** One independently generated wall-ring component per window opening. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Simple House 16|Opening Objects")
	TArray<TObjectPtr<UDynamicMeshComponent>> WindowObjectComponents;

	/** One independently generated wall-ring component per door opening. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Simple House 16|Opening Objects")
	TArray<TObjectPtr<UDynamicMeshComponent>> DoorObjectComponents;

	/** Recreates every part as an independent Dynamic Mesh component. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Simple House 16")
	void RebuildMesh();

	/** Immediately reapplies all visibility flags without rebuilding geometry. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Simple House 16|Parts")
	void ApplyPartVisibility();

	/** Immediately shows or hides one part in the editor or at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Simple House 16|Parts")
	void SetPartVisibility(ESimpleHouse16Part Part, bool bVisible);

	/** Returns the Dynamic Mesh component belonging to the requested part. */
	UFUNCTION(BlueprintPure, Category = "Simple House 16|Parts")
	UDynamicMeshComponent* GetPartComponent(ESimpleHouse16Part Part) const;

private:
	void SynchronizeFloorWindowSettings();
	void FinalizeComponent(UDynamicMeshComponent* Component) const;
};
