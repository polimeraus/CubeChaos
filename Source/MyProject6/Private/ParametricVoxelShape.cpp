#include "ParametricVoxelShape.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 MaxVoxelCount = 16384;
	constexpr int32 MaxEdgeCount = 131072;

	float SafeVoxelValue(float Value, float Min, float Max, float Default)
	{
		return FMath::Clamp(FMath::IsFinite(Value) ? Value : Default, Min, Max);
	}
}

AParametricVoxelShape::AParametricVoxelShape()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ShapeRoot")));
	RootComponent->SetMobility(EComponentMobility::Movable);
	WireEdges = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WireEdges"));
	SolidCubes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SolidCubes"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(
		TEXT("/Game/Structures/Parametric/VoxelShape/Materials/M_VoxelShapeGlow.M_VoxelShapeGlow"));
	if (Glow.Succeeded()) { ShapeMaterial = Glow.Object; }
	for (UInstancedStaticMeshComponent* Component : {WireEdges.Get(), SolidCubes.Get()})
	{
		Component->SetupAttachment(RootComponent);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		if (Cube.Succeeded()) { Component->SetStaticMesh(Cube.Object); }
	}
	CustomPatternRows = {TEXT("01110"), TEXT("11011"), TEXT("11111"), TEXT("01110"), TEXT("00100")};
}

void AParametricVoxelShape::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildShape();
}

void AParametricVoxelShape::BeginPlay()
{
	Super::BeginPlay();
	// Saved/cooked level actors do not necessarily run OnConstruction on load.
	RebuildShape();
}

void AParametricVoxelShape::ConfigureShape(EVoxelShapePreset NewShape, int32 NewWidthCubes, int32 NewDepthCubes)
{
	Shape = NewShape;
	WidthCubes = NewWidthCubes;
	DepthCubes = NewDepthCubes;
	RebuildShape();
}

TArray<FString> AParametricVoxelShape::GetPattern() const
{
	switch (Shape)
	{
	case EVoxelShapePreset::Heart:
		return {TEXT("001110000011100"), TEXT("011111000111110"), TEXT("111111101111111"),
			TEXT("111111111111111"), TEXT("111111111111111"), TEXT("111111111111111"),
			TEXT("011111111111110"), TEXT("001111111111100"), TEXT("000111111111000"),
			TEXT("000011111110000"), TEXT("000001111100000"), TEXT("000000111000000"), TEXT("000000010000000")};
	case EVoxelShapePreset::InvaderCrab:
		return {TEXT("00100000100"), TEXT("00010001000"), TEXT("00111111100"), TEXT("01101110110"),
			TEXT("11111111111"), TEXT("10111111101"), TEXT("10100000101"), TEXT("00011011000")};
	case EVoxelShapePreset::InvaderSquid:
		return {TEXT("00011000"), TEXT("00111100"), TEXT("01111110"), TEXT("11011011"),
			TEXT("11111111"), TEXT("00100100"), TEXT("01011010"), TEXT("10100101")};
	case EVoxelShapePreset::Ghost:
		return {TEXT("00011111000"), TEXT("00111111100"), TEXT("01111111110"), TEXT("11111111111"),
			TEXT("11001110011"), TEXT("11001110011"), TEXT("11111111111"), TEXT("11111111111"),
			TEXT("11111111111"), TEXT("11101110111"), TEXT("11000100011")};
	case EVoxelShapePreset::Chomper:
		return {TEXT("0000111110000"), TEXT("0011111111100"), TEXT("0111111111110"), TEXT("0111111111111"),
			TEXT("1111111000000"), TEXT("1111100000000"), TEXT("1111000000000"), TEXT("1111100000000"),
			TEXT("1111111000000"), TEXT("0111111111111"), TEXT("0111111111110"), TEXT("0011111111100"), TEXT("0000111110000")};
	case EVoxelShapePreset::Star:
		return {TEXT("0000001000000"), TEXT("0000001000000"), TEXT("0000011100000"), TEXT("0000011100000"),
			TEXT("1111111111111"), TEXT("0111111111110"), TEXT("0011111111100"), TEXT("0001111111000"),
			TEXT("0001111111000"), TEXT("0011110111100"), TEXT("0011100011100"), TEXT("0111000001110"), TEXT("0110000000110")};
	case EVoxelShapePreset::Custom:
		return CustomPatternRows;
	default:
		return {};
	}
}

void AParametricVoxelShape::UpdateMaterial()
{
	if (!ShapeMaterial) { ShapeMaterialInstance = nullptr; }
	else if (!ShapeMaterialInstance || ShapeMaterialInstance->Parent != ShapeMaterial)
	{
		ShapeMaterialInstance = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
	}
	if (ShapeMaterialInstance)
	{
		ShapeMaterialInstance->SetVectorParameterValue(TEXT("Color"), Color * EmissiveIntensity);
		ShapeMaterialInstance->SetVectorParameterValue(TEXT("BaseColor"), Color);
		ShapeMaterialInstance->SetVectorParameterValue(TEXT("EmissiveColor"), Color * EmissiveIntensity);
		ShapeMaterialInstance->SetScalarParameterValue(TEXT("EmissiveIntensity"), EmissiveIntensity);
	}
	WireEdges->SetMaterial(0, ShapeMaterialInstance ? ShapeMaterialInstance.Get() : ShapeMaterial.Get());
	SolidCubes->SetMaterial(0, ShapeMaterialInstance ? ShapeMaterialInstance.Get() : ShapeMaterial.Get());
}

void AParametricVoxelShape::RebuildShape()
{
	WidthCubes = FMath::Clamp(WidthCubes, 1, 128);
	HeightCubes = FMath::Clamp(HeightCubes, 1, 128);
	DepthCubes = FMath::Clamp(DepthCubes, 1, 32);
	CubeSize = SafeVoxelValue(CubeSize, 1, 1000, 40);
	CubeGap = SafeVoxelValue(CubeGap, 0, 1000, 0);
	WireThickness = SafeVoxelValue(WireThickness, .1f, CubeSize * .5f, 1.5f);
	EmissiveIntensity = SafeVoxelValue(EmissiveIntensity, 0, 100, 5);
	for (int32 Channel = 0; Channel < 4; ++Channel)
	{
		Color.Component(Channel) = SafeVoxelValue(Color.Component(Channel), 0, 1, 1);
	}
	WireEdges->ClearInstances();
	SolidCubes->ClearInstances();
	GeneratedGrid = FIntVector::ZeroValue;
	GeneratedSize = FVector::ZeroVector;
	GeneratedCubeCount = GeneratedEdgeCount = 0;
	GenerationWarning.Reset();
	UpdateMaterial();

	const TArray<FString> Pattern = GetPattern();
	if (Pattern.IsEmpty() || Pattern.Num() > 128 || Pattern[0].IsEmpty() || Pattern[0].Len() > 128)
	{
		GenerationWarning = TEXT("Pattern must contain 1 to 128 rows and columns.");
		return;
	}
	const int32 SourceWidth = Pattern[0].Len();
	for (const FString& Row : Pattern)
	{
		if (Row.Len() != SourceWidth)
		{
			GenerationWarning = TEXT("All custom pattern rows must have the same length.");
			return;
		}
		for (TCHAR Cell : Row)
		{
			if (!FString(TEXT("01.#Xx ")).Contains(FString::Chr(Cell)))
			{
				GenerationWarning = TEXT("Use 1 / # / X for filled pixels and 0 / . / space for empty pixels.");
				return;
			}
		}
	}
	const int32 Height = bPreserveAspectRatio
		? FMath::Clamp(FMath::RoundToInt(WidthCubes * double(Pattern.Num()) / SourceWidth), 1, 128) : HeightCubes;
	TArray<FIntPoint> Pixels;
	for (int32 Z = 0; Z < Height; ++Z)
	{
		const int32 Row = FMath::Min(int32((Height - 1 - Z + .5) * Pattern.Num() / Height), Pattern.Num() - 1);
		for (int32 X = 0; X < WidthCubes; ++X)
		{
			const int32 Column = FMath::Min(int32((X + .5) * SourceWidth / WidthCubes), SourceWidth - 1);
			const TCHAR Cell = Pattern[Row][Column];
			if (Cell == '1' || Cell == '#' || Cell == 'X' || Cell == 'x') { Pixels.Add(FIntPoint(X, Z)); }
		}
	}
	if (Pixels.IsEmpty())
	{
		GenerationWarning = TEXT("No filled pixels at this resolution. Increase width/height or edit the pattern.");
		return;
	}
	const int32 Count = Pixels.Num() * DepthCubes;
	if (Count > MaxVoxelCount)
	{
		GenerationWarning = FString::Printf(TEXT("Requested %d cubes; limit is %d. Reduce width, height or depth."), Count, MaxVoxelCount);
		return;
	}
	const double Pitch = CubeSize + CubeGap;
	const FVector Size(WidthCubes * Pitch - CubeGap, DepthCubes * Pitch - CubeGap, Height * Pitch - CubeGap);
	const FVector Origin(-Size.X * .5, -Size.Y * .5, bCenterPivot ? -Size.Z * .5 : 0);
	TArray<FTransform> Transforms;
	const bool bWire = Appearance == EVoxelShapeAppearance::NeonWireCubes;
	Transforms.Reserve(bWire ? FMath::Min(Count * 12, MaxEdgeCount) : Count);
	TSet<FIntVector> UniqueEdges;
	for (const FIntPoint& Pixel : Pixels)
	{
		for (int32 Y = 0; Y < DepthCubes; ++Y)
		{
			const FIntVector GridCell(Pixel.X, Y, Pixel.Y);
			const FVector Lower = Origin + FVector(GridCell) * Pitch;
			if (!bWire)
			{
				Transforms.Emplace(FQuat::Identity, Lower + FVector(CubeSize * .5), FVector(CubeSize / 100.0));
				continue;
			}
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const int32 B = (Axis + 1) % 3, C = (Axis + 2) % 3;
				for (int32 SideB = 0; SideB < 2; ++SideB)
				{
					for (int32 SideC = 0; SideC < 2; ++SideC)
					{
						FIntVector Start = GridCell;
						Start[B] += SideB;
						Start[C] += SideC;
						// The axis is part of the key. Touching cubes share a single glowing edge.
						const FIntVector Key(Start.X * 3 + Axis, Start.Y, Start.Z);
						if (CubeGap == 0)
						{
							if (UniqueEdges.Contains(Key)) { continue; }
							UniqueEdges.Add(Key);
						}
						if (Transforms.Num() == MaxEdgeCount)
						{
							GenerationWarning = TEXT("Neon edge limit exceeded (131072). Reduce resolution/depth or use solid cubes.");
							return;
						}
						FVector Position = Lower, Dimensions(WireThickness);
						Position[Axis] += CubeSize * .5;
						Position[B] += SideB * CubeSize;
						Position[C] += SideC * CubeSize;
						Dimensions[Axis] = CubeSize + WireThickness;
						Transforms.Emplace(FQuat::Identity, Position, Dimensions / 100.0);
					}
				}
			}
		}
	}
	(bWire ? WireEdges : SolidCubes)->AddInstances(Transforms, false, false, false);
	GeneratedGrid = FIntVector(WidthCubes, DepthCubes, Height);
	GeneratedSize = Size;
	GeneratedCubeCount = Count;
	GeneratedEdgeCount = bWire ? Transforms.Num() : 0;
}
