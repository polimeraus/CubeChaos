// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleDecorativeFloor.h"

#include "Algo/Reverse.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Math/RandomStream.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace
{
	constexpr float MinimumDecorativeFloorDimension = 1.0f;
	constexpr float ShapeTolerance = 0.0001f;
	constexpr float ExactShapeSpacingTolerance = 0.01f;
	constexpr double ShapePolygonTolerance = 0.01;
	constexpr double HeartSurfaceLift = 0.20;
	constexpr double HeartLayerSeparation = 0.20;
	constexpr double HeartPolygonTolerance = 0.000001;

	struct FHeartTriangle
	{
		FVector2D A;
		FVector2D B;
		FVector2D C;
		FVector2D Min;
		FVector2D Max;
	};

	FVector2D RotatePoint(const FVector2D& Point, float AngleRadians)
	{
		const float CosAngle = FMath::Cos(AngleRadians);
		const float SinAngle = FMath::Sin(AngleRadians);
		return FVector2D(
			Point.X * CosAngle - Point.Y * SinAngle,
			Point.X * SinAngle + Point.Y * CosAngle);
	}

	TArray<FVector2D> BuildFlowerEllipseOutline(
		const FVector2D& FlowerCenter,
		const FVector2D& FlowerHalfSize,
		const FVector2D& EllipseCenter,
		const FVector2D& EllipseHalfSize,
		float EllipseRotationRadians,
		float FlowerRotationRadians,
		int32 Resolution)
	{
		const int32 SafeResolution = FMath::Clamp(Resolution, 8, 64);
		const FVector2D Radial(
			FMath::Cos(EllipseRotationRadians),
			FMath::Sin(EllipseRotationRadians));
		const FVector2D Tangent(-Radial.Y, Radial.X);

		TArray<FVector2D> Outline;
		Outline.Reserve(SafeResolution);
		for (int32 Index = 0; Index < SafeResolution; ++Index)
		{
			const float T = UE_TWO_PI * static_cast<float>(Index) / SafeResolution;
			const FVector2D LocalPoint = EllipseCenter +
				Radial * (FMath::Cos(T) * EllipseHalfSize.X) +
				Tangent * (FMath::Sin(T) * EllipseHalfSize.Y);
			Outline.Add(FlowerCenter + RotatePoint(LocalPoint * FlowerHalfSize, FlowerRotationRadians));
		}
		return Outline;
	}

	TArray<FVector2D> BuildStarOutline(
		const FVector2D& StarSize,
		const FVector2D& StarOffset,
		float StarRotationDegrees,
		float Scale,
		int32 PointCount,
		float InnerRadius)
	{
		const int32 SafePointCount = FMath::Clamp(PointCount, 3, 32);
		const float SafeInnerRadius = FMath::Clamp(InnerRadius, 0.1f, 0.9f);
		const FVector2D HalfSize(
			FMath::Max(FMath::Abs(StarSize.X) * Scale, MinimumDecorativeFloorDimension) * 0.5,
			FMath::Max(FMath::Abs(StarSize.Y) * Scale, MinimumDecorativeFloorDimension) * 0.5);
		const float RotationRadians = FMath::DegreesToRadians(StarRotationDegrees);

		TArray<FVector2D> Outline;
		Outline.Reserve(SafePointCount * 2);
		FBox2D RotatedBounds(EForceInit::ForceInit);
		for (int32 Index = 0; Index < SafePointCount * 2; ++Index)
		{
			const float Radius = (Index & 1) == 0 ? 1.0f : SafeInnerRadius;
			const float Angle = UE_HALF_PI + RotationRadians + Index * PI / SafePointCount;
			const FVector2D Point(
				FMath::Cos(Angle) * HalfSize.X * Radius,
				FMath::Sin(Angle) * HalfSize.Y * Radius);
			Outline.Add(Point);
			RotatedBounds += Point;
		}

		const FVector2D CenterCorrection = StarOffset - RotatedBounds.GetCenter();
		for (FVector2D& Point : Outline)
		{
			Point += CenterCorrection;
		}
		return Outline;
	}

	double Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	double SignedPolygonArea(const TArray<FVector2D>& Polygon)
	{
		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector2D& A = Polygon[Index];
			const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
			TwiceArea += Cross2D(A, B);
		}
		return TwiceArea * 0.5;
	}

	bool IsPointOnSegment(
		const FVector2D& Point,
		const FVector2D& Start,
		const FVector2D& End,
		double Tolerance)
	{
		const FVector2D Segment = End - Start;
		const double SegmentLengthSquared = Segment.SizeSquared();
		if (SegmentLengthSquared <= FMath::Square(Tolerance))
		{
			return Point.Equals(Start, Tolerance);
		}

		const double Alpha = FMath::Clamp(
			FVector2D::DotProduct(Point - Start, Segment) / SegmentLengthSquared,
			0.0,
			1.0);
		return (Point - (Start + Segment * Alpha)).SizeSquared() <= FMath::Square(Tolerance);
	}

	double GetBoundaryDistance(const FVector2D& Point, int32 BoundaryIndex, double BoundaryValue)
	{
		switch (BoundaryIndex)
		{
		case 0: return Point.X - BoundaryValue;
		case 1: return BoundaryValue - Point.X;
		case 2: return Point.Y - BoundaryValue;
		default: return BoundaryValue - Point.Y;
		}
	}

	void ClipPolygonAgainstBoundary(
		TArray<FVector2D>& Polygon,
		int32 BoundaryIndex,
		double BoundaryValue)
	{
		if (Polygon.IsEmpty())
		{
			return;
		}

		const TArray<FVector2D> Input = Polygon;
		Polygon.Reset();
		FVector2D Previous = Input.Last();
		double PreviousDistance = GetBoundaryDistance(Previous, BoundaryIndex, BoundaryValue);
		bool bPreviousInside = PreviousDistance >= -ShapePolygonTolerance;

		for (const FVector2D& Current : Input)
		{
			const double CurrentDistance = GetBoundaryDistance(Current, BoundaryIndex, BoundaryValue);
			const bool bCurrentInside = CurrentDistance >= -ShapePolygonTolerance;
			if (bCurrentInside != bPreviousInside)
			{
				const double Denominator = PreviousDistance - CurrentDistance;
				if (!FMath::IsNearlyZero(Denominator))
				{
					const double Alpha = FMath::Clamp(PreviousDistance / Denominator, 0.0, 1.0);
					Polygon.Add(Previous + (Current - Previous) * Alpha);
				}
			}
			if (bCurrentInside)
			{
				Polygon.Add(Current);
			}
			Previous = Current;
			PreviousDistance = CurrentDistance;
			bPreviousInside = bCurrentInside;
		}
	}

	bool ClipSegmentToRectangle(
		const FVector2D& InputStart,
		const FVector2D& InputEnd,
		double MinX,
		double MinY,
		double MaxX,
		double MaxY,
		FVector2D& OutStart,
		FVector2D& OutEnd)
	{
		OutStart = InputStart;
		OutEnd = InputEnd;
		const double BoundaryValues[4] = { MinX, MaxX, MinY, MaxY };
		for (int32 BoundaryIndex = 0; BoundaryIndex < 4; ++BoundaryIndex)
		{
			const double StartDistance = GetBoundaryDistance(
				OutStart, BoundaryIndex, BoundaryValues[BoundaryIndex]);
			const double EndDistance = GetBoundaryDistance(
				OutEnd, BoundaryIndex, BoundaryValues[BoundaryIndex]);
			if (StartDistance < -ShapePolygonTolerance && EndDistance < -ShapePolygonTolerance)
			{
				return false;
			}
			if ((StartDistance < 0.0) != (EndDistance < 0.0))
			{
				const double Denominator = StartDistance - EndDistance;
				if (!FMath::IsNearlyZero(Denominator))
				{
					const FVector2D Intersection = OutStart +
						(OutEnd - OutStart) * FMath::Clamp(StartDistance / Denominator, 0.0, 1.0);
					if (StartDistance < 0.0)
					{
						OutStart = Intersection;
					}
					else
					{
						OutEnd = Intersection;
					}
				}
			}
		}
		return true;
	}

	void RemoveDuplicatePolygonPoints(TArray<FVector2D>& Polygon)
	{
		for (int32 Index = Polygon.Num() - 1; Index >= 0 && Polygon.Num() > 1; --Index)
		{
			const int32 PreviousIndex = (Index - 1 + Polygon.Num()) % Polygon.Num();
			if (Polygon[Index].Equals(Polygon[PreviousIndex], ShapePolygonTolerance))
			{
				Polygon.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
	}

	bool IsPointInsideCounterClockwiseTriangle(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C)
	{
		return Cross2D(B - A, Point - A) >= -HeartPolygonTolerance &&
			Cross2D(C - B, Point - B) >= -HeartPolygonTolerance &&
			Cross2D(A - C, Point - C) >= -HeartPolygonTolerance;
	}

	TArray<FVector2D> BuildParametricHeartOutline(
		const FVector2D& HeartSize,
		const FVector2D& HeartOffset,
		float HeartRotationDegrees,
		float Scale,
		int32 Resolution)
	{
		const int32 SafeResolution = FMath::Clamp(Resolution, 32, 256);
		TArray<FVector2D> RawPoints;
		RawPoints.Reserve(SafeResolution);
		FVector2D RawMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D RawMax(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());

		// This classic parametric curve gives two round, symmetric lobes, a clean center notch
		// and a single sharp lower tip. Normalizing the sampled bounds keeps HeartSize exact.
		for (int32 Index = 0; Index < SafeResolution; ++Index)
		{
			const double T = UE_TWO_PI * static_cast<double>(Index) / SafeResolution;
			const double SinT = FMath::Sin(T);
			const double X = 16.0 * SinT * SinT * SinT;
			const double Y = 13.0 * FMath::Cos(T) -
				5.0 * FMath::Cos(2.0 * T) -
				2.0 * FMath::Cos(3.0 * T) -
				FMath::Cos(4.0 * T);
			const FVector2D Point(X, Y);
			RawPoints.Add(Point);
			RawMin.X = FMath::Min(RawMin.X, Point.X);
			RawMin.Y = FMath::Min(RawMin.Y, Point.Y);
			RawMax.X = FMath::Max(RawMax.X, Point.X);
			RawMax.Y = FMath::Max(RawMax.Y, Point.Y);
		}

		const FVector2D RawCenter = (RawMin + RawMax) * 0.5;
		const FVector2D RawHalfSize = (RawMax - RawMin) * 0.5;
		const FVector2D TargetHalfSize(
			FMath::Max(HeartSize.X * Scale, MinimumDecorativeFloorDimension) * 0.5,
			FMath::Max(HeartSize.Y * Scale, MinimumDecorativeFloorDimension) * 0.5);
		const float RotationRadians = FMath::DegreesToRadians(HeartRotationDegrees);

		TArray<FVector2D> Outline;
		Outline.Reserve(SafeResolution);
		FBox2D RotatedBounds(EForceInit::ForceInit);
		for (const FVector2D& RawPoint : RawPoints)
		{
			const FVector2D Normalized(
				(RawPoint.X - RawCenter.X) / FMath::Max(RawHalfSize.X, HeartPolygonTolerance),
				(RawPoint.Y - RawCenter.Y) / FMath::Max(RawHalfSize.Y, HeartPolygonTolerance));
			const FVector2D Point = RotatePoint(Normalized * TargetHalfSize, RotationRadians);
			Outline.Add(Point);
			RotatedBounds += Point;
		}

		const FVector2D CenterCorrection = HeartOffset - RotatedBounds.GetCenter();
		for (FVector2D& Point : Outline)
		{
			Point += CenterCorrection;
		}
		return Outline;
	}

	TArray<FHeartTriangle> TriangulateHeartOutline(const TArray<FVector2D>& Outline)
	{
		TArray<FHeartTriangle> Triangles;
		if (Outline.Num() < 3)
		{
			return Triangles;
		}

		TArray<int32> Remaining;
		Remaining.Reserve(Outline.Num());
		const bool bAlreadyCounterClockwise = SignedPolygonArea(Outline) > 0.0;
		for (int32 Index = 0; Index < Outline.Num(); ++Index)
		{
			Remaining.Add(bAlreadyCounterClockwise ? Index : Outline.Num() - 1 - Index);
		}

		Triangles.Reserve(Outline.Num() - 2);
		int32 Guard = Outline.Num() * Outline.Num();
		while (Remaining.Num() > 3 && Guard-- > 0)
		{
			bool bRemovedEar = false;
			for (int32 EarIndex = 0; EarIndex < Remaining.Num(); ++EarIndex)
			{
				const int32 Previous = Remaining[(EarIndex - 1 + Remaining.Num()) % Remaining.Num()];
				const int32 Current = Remaining[EarIndex];
				const int32 Next = Remaining[(EarIndex + 1) % Remaining.Num()];
				const FVector2D& A = Outline[Previous];
				const FVector2D& B = Outline[Current];
				const FVector2D& C = Outline[Next];
				if (Cross2D(B - A, C - B) <= HeartPolygonTolerance)
				{
					continue;
				}

				bool bContainsAnotherVertex = false;
				for (const int32 TestIndex : Remaining)
				{
					if (TestIndex != Previous && TestIndex != Current && TestIndex != Next &&
						IsPointInsideCounterClockwiseTriangle(Outline[TestIndex], A, B, C))
					{
						bContainsAnotherVertex = true;
						break;
					}
				}
				if (bContainsAnotherVertex)
				{
					continue;
				}

				FHeartTriangle& Triangle = Triangles.Emplace_GetRef();
				Triangle.A = A;
				Triangle.B = B;
				Triangle.C = C;
				Triangle.Min = FVector2D(
					FMath::Min3(A.X, B.X, C.X),
					FMath::Min3(A.Y, B.Y, C.Y));
				Triangle.Max = FVector2D(
					FMath::Max3(A.X, B.X, C.X),
					FMath::Max3(A.Y, B.Y, C.Y));
				Remaining.RemoveAt(EarIndex, 1, EAllowShrinking::No);
				bRemovedEar = true;
				break;
			}
			if (!bRemovedEar)
			{
				break;
			}
		}

		if (Remaining.Num() == 3)
		{
			FHeartTriangle& Triangle = Triangles.Emplace_GetRef();
			Triangle.A = Outline[Remaining[0]];
			Triangle.B = Outline[Remaining[1]];
			Triangle.C = Outline[Remaining[2]];
			Triangle.Min = FVector2D(
				FMath::Min3(Triangle.A.X, Triangle.B.X, Triangle.C.X),
				FMath::Min3(Triangle.A.Y, Triangle.B.Y, Triangle.C.Y));
			Triangle.Max = FVector2D(
				FMath::Max3(Triangle.A.X, Triangle.B.X, Triangle.C.X),
				FMath::Max3(Triangle.A.Y, Triangle.B.Y, Triangle.C.Y));
		}
		return Triangles;
	}

	void ClipPolygonAgainstAxis(
		const TArray<FVector2D>& Input,
		TArray<FVector2D>& Output,
		bool bXAxis,
		double Boundary,
		bool bKeepGreater)
	{
		Output.Reset();
		if (Input.IsEmpty())
		{
			return;
		}

		auto Coordinate = [bXAxis](const FVector2D& Point)
		{
			return bXAxis ? Point.X : Point.Y;
		};
		auto IsInside = [bKeepGreater, Boundary](double Value)
		{
			return bKeepGreater ? Value >= Boundary - HeartPolygonTolerance : Value <= Boundary + HeartPolygonTolerance;
		};

		FVector2D Previous = Input.Last();
		double PreviousCoordinate = Coordinate(Previous);
		bool bPreviousInside = IsInside(PreviousCoordinate);
		for (const FVector2D& Current : Input)
		{
			const double CurrentCoordinate = Coordinate(Current);
			const bool bCurrentInside = IsInside(CurrentCoordinate);
			if (bCurrentInside != bPreviousInside)
			{
				const double Denominator = CurrentCoordinate - PreviousCoordinate;
				const double Alpha = FMath::IsNearlyZero(Denominator)
					? 0.0
					: FMath::Clamp((Boundary - PreviousCoordinate) / Denominator, 0.0, 1.0);
				FVector2D Intersection = FMath::Lerp(Previous, Current, Alpha);
				if (bXAxis)
				{
					Intersection.X = Boundary;
				}
				else
				{
					Intersection.Y = Boundary;
				}
				Output.Add(Intersection);
			}
			if (bCurrentInside)
			{
				Output.Add(Current);
			}
			Previous = Current;
			PreviousCoordinate = CurrentCoordinate;
			bPreviousInside = bCurrentInside;
		}
	}

	TArray<FVector2D> ClipTriangleToRectangle(
		const FHeartTriangle& Triangle,
		const FVector2D& RectangleMin,
		const FVector2D& RectangleMax)
	{
		TArray<FVector2D> Polygon = { Triangle.A, Triangle.B, Triangle.C };
		TArray<FVector2D> Scratch;
		ClipPolygonAgainstAxis(Polygon, Scratch, true, RectangleMin.X, true);
		ClipPolygonAgainstAxis(Scratch, Polygon, true, RectangleMax.X, false);
		ClipPolygonAgainstAxis(Polygon, Scratch, false, RectangleMin.Y, true);
		ClipPolygonAgainstAxis(Scratch, Polygon, false, RectangleMax.Y, false);
		return Polygon;
	}

	TArray<FVector2D> ClipPolygonToConvexPolygon(
		const TArray<FVector2D>& Subject,
		const TArray<FVector2D>& ClipPolygon)
	{
		TArray<FVector2D> Result = Subject;
		if (Result.Num() < 3 || ClipPolygon.Num() < 3)
		{
			Result.Reset();
			return Result;
		}

		for (int32 ClipIndex = 0; ClipIndex < ClipPolygon.Num() && !Result.IsEmpty(); ++ClipIndex)
		{
			const FVector2D& ClipA = ClipPolygon[ClipIndex];
			const FVector2D& ClipB = ClipPolygon[(ClipIndex + 1) % ClipPolygon.Num()];
			const FVector2D ClipEdge = ClipB - ClipA;
			const TArray<FVector2D> Input = Result;
			Result.Reset();

			FVector2D Previous = Input.Last();
			double PreviousDistance = Cross2D(ClipEdge, Previous - ClipA);
			bool bPreviousInside = PreviousDistance >= -HeartPolygonTolerance;
			for (const FVector2D& Current : Input)
			{
				const double CurrentDistance = Cross2D(ClipEdge, Current - ClipA);
				const bool bCurrentInside = CurrentDistance >= -HeartPolygonTolerance;
				if (bCurrentInside != bPreviousInside)
				{
					const double Denominator = PreviousDistance - CurrentDistance;
					if (!FMath::IsNearlyZero(Denominator))
					{
						const double Alpha = FMath::Clamp(PreviousDistance / Denominator, 0.0, 1.0);
						Result.Add(Previous + (Current - Previous) * Alpha);
					}
				}
				if (bCurrentInside)
				{
					Result.Add(Current);
				}
				Previous = Current;
				PreviousDistance = CurrentDistance;
				bPreviousInside = bCurrentInside;
			}
		}
		return Result;
	}
}

AChunkedDestructibleDecorativeFloor::AChunkedDestructibleDecorativeFloor()
{
	AreaSize = ShapeSize;
	CellDimensions = FVector(100.0, 100.0, 35.0);
	FloorPattern = EChunkedFloorPattern::Solid;
	// Decorative outlines and overlays are substantially denser than rectangular floor cells.
	// Smaller chunks limit a hit to a local rebuild while retaining identical geometry/collision.
	ChunkSideLength = 10;

	// Retain the base floor's dark material as the small heart, and place the bundled glow
	// material in slot 1 so a newly placed actor immediately demonstrates the intended layering.
	UMaterialInterface* DarkMaterial = FloorMaterials.IsValidIndex(1) ? FloorMaterials[1].Get() : nullptr;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMaterialFinder(
		TEXT("/Game/GameplayActors/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
	if (GlowMaterialFinder.Succeeded())
	{
		if (FloorMaterials.IsValidIndex(1))
		{
			FloorMaterials[1] = GlowMaterialFinder.Object;
		}
		else
		{
			FloorMaterials.Add(GlowMaterialFinder.Object);
		}
	}
	if (DarkMaterial)
	{
		while (FloorMaterials.Num() < 2)
		{
			FloorMaterials.Add(DarkMaterial);
		}
		FloorMaterials.Add(DarkMaterial);
	}
}

void AChunkedDestructibleDecorativeFloor::PostLoad()
{
	Super::PostLoad();

	const FChunkedHeartMotifSettings DefaultHeartSettings;
	const FChunkedStarMotifSettings DefaultStarSettings;
	const bool bHeartSettingsAreDefault =
		HeartMotifSettings.BackgroundMaterialIndex == DefaultHeartSettings.BackgroundMaterialIndex &&
		HeartMotifSettings.AccentMaterialIndex == DefaultHeartSettings.AccentMaterialIndex &&
		HeartMotifSettings.InnerMaterialIndex == DefaultHeartSettings.InnerMaterialIndex &&
		HeartMotifSettings.Size == DefaultHeartSettings.Size &&
		HeartMotifSettings.InnerScale == DefaultHeartSettings.InnerScale &&
		HeartMotifSettings.bAutoCenter == DefaultHeartSettings.bAutoCenter &&
		HeartMotifSettings.Offset == DefaultHeartSettings.Offset &&
		HeartMotifSettings.RotationDegrees == DefaultHeartSettings.RotationDegrees &&
		HeartMotifSettings.CurveResolution == DefaultHeartSettings.CurveResolution;
	const bool bStarSettingsAreDefault =
		StarMotifSettings.BackgroundMaterialIndex == DefaultStarSettings.BackgroundMaterialIndex &&
		StarMotifSettings.AccentMaterialIndex == DefaultStarSettings.AccentMaterialIndex &&
		StarMotifSettings.InnerMaterialIndex == DefaultStarSettings.InnerMaterialIndex &&
		StarMotifSettings.Size == DefaultStarSettings.Size &&
		StarMotifSettings.PointCount == DefaultStarSettings.PointCount &&
		StarMotifSettings.InnerRadius == DefaultStarSettings.InnerRadius &&
		StarMotifSettings.InnerScale == DefaultStarSettings.InnerScale &&
		StarMotifSettings.bAutoCenter == DefaultStarSettings.bAutoCenter &&
		StarMotifSettings.Offset == DefaultStarSettings.Offset &&
		StarMotifSettings.RotationDegrees == DefaultStarSettings.RotationDegrees;
	const bool bLegacySettingsAreCustomized =
		BackgroundMaterialIndex != 0 ||
		HeartAccentMaterialIndex != 1 ||
		InnerHeartMaterialIndex != 2 ||
		HeartSize != DefaultHeartSettings.Size ||
		InnerHeartScale != DefaultHeartSettings.InnerScale ||
		HeartOffset != DefaultHeartSettings.Offset ||
		HeartRotationDegrees != DefaultHeartSettings.RotationDegrees ||
		HeartCurveResolution != DefaultHeartSettings.CurveResolution ||
		StarAccentMaterialIndex != 1 ||
		InnerStarMaterialIndex != 2 ||
		MotifStarSize != DefaultStarSettings.Size ||
		MotifStarPointCount != DefaultStarSettings.PointCount ||
		MotifStarInnerRadius != DefaultStarSettings.InnerRadius ||
		InnerStarScale != DefaultStarSettings.InnerScale ||
		MotifStarOffset != DefaultStarSettings.Offset ||
		MotifStarRotationDegrees != DefaultStarSettings.RotationDegrees;

	if (bHeartSettingsAreDefault && bStarSettingsAreDefault && bLegacySettingsAreCustomized)
	{
		// Preserve values already serialized by maps and Blueprints created before the
		// native motif groups existed. Comparing values also handles older class defaults,
		// where a newly introduced migration flag may inherit its current CDO value.
		SyncMotifSettingsFromLegacyFields();
	}
	else
	{
		SyncLegacyFieldsFromMotifSettings();
	}
	bMotifSettingsMigrated = true;
}

void AChunkedDestructibleDecorativeFloor::OnConstruction(const FTransform& Transform)
{
	// The inherited construction pass builds the generated mesh from the compatibility
	// fields, so mirror the selected native group before that pass starts.
	SyncLegacyFieldsFromMotifSettings();
	Super::OnConstruction(Transform);
}

#if WITH_EDITOR
bool AChunkedDestructibleDecorativeFloor::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty && InProperty->GetFName() == FName(TEXT("AreaSize")))
	{
		return false;
	}
	return Super::CanEditChange(InProperty);
}

void AChunkedDestructibleDecorativeFloor::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	bMotifSettingsMigrated = true;
	SyncLegacyFieldsFromMotifSettings();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void AChunkedDestructibleDecorativeFloor::SyncMotifSettingsFromLegacyFields()
{
	HeartMotifSettings.BackgroundMaterialIndex = BackgroundMaterialIndex;
	HeartMotifSettings.AccentMaterialIndex = HeartAccentMaterialIndex;
	HeartMotifSettings.InnerMaterialIndex = InnerHeartMaterialIndex;
	HeartMotifSettings.Size = HeartSize;
	HeartMotifSettings.InnerScale = InnerHeartScale;
	HeartMotifSettings.bAutoCenter = HeartOffset.IsNearlyZero();
	HeartMotifSettings.Offset = HeartOffset;
	HeartMotifSettings.RotationDegrees = HeartRotationDegrees;
	HeartMotifSettings.CurveResolution = HeartCurveResolution;

	StarMotifSettings.BackgroundMaterialIndex = BackgroundMaterialIndex;
	StarMotifSettings.AccentMaterialIndex = StarAccentMaterialIndex;
	StarMotifSettings.InnerMaterialIndex = InnerStarMaterialIndex;
	StarMotifSettings.Size = MotifStarSize;
	StarMotifSettings.PointCount = MotifStarPointCount;
	StarMotifSettings.InnerRadius = MotifStarInnerRadius;
	StarMotifSettings.InnerScale = InnerStarScale;
	StarMotifSettings.bAutoCenter = MotifStarOffset.IsNearlyZero();
	StarMotifSettings.Offset = MotifStarOffset;
	StarMotifSettings.RotationDegrees = MotifStarRotationDegrees;
	CenterMotifsInShape();
}

void AChunkedDestructibleDecorativeFloor::SyncLegacyFieldsFromMotifSettings()
{
	CenterMotifsInShape();
	HeartAccentMaterialIndex = HeartMotifSettings.AccentMaterialIndex;
	InnerHeartMaterialIndex = HeartMotifSettings.InnerMaterialIndex;
	HeartSize = HeartMotifSettings.Size;
	InnerHeartScale = HeartMotifSettings.InnerScale;
	HeartOffset = HeartMotifSettings.Offset;
	HeartRotationDegrees = HeartMotifSettings.RotationDegrees;
	HeartCurveResolution = HeartMotifSettings.CurveResolution;

	StarAccentMaterialIndex = StarMotifSettings.AccentMaterialIndex;
	InnerStarMaterialIndex = StarMotifSettings.InnerMaterialIndex;
	MotifStarSize = StarMotifSettings.Size;
	MotifStarPointCount = StarMotifSettings.PointCount;
	MotifStarInnerRadius = StarMotifSettings.InnerRadius;
	InnerStarScale = StarMotifSettings.InnerScale;
	MotifStarOffset = StarMotifSettings.Offset;
	MotifStarRotationDegrees = StarMotifSettings.RotationDegrees;

	if (FloorPattern == EChunkedFloorPattern::HeartMotif)
	{
		BackgroundMaterialIndex = HeartMotifSettings.BackgroundMaterialIndex;
	}
	else if (FloorPattern == EChunkedFloorPattern::StarMotif)
	{
		BackgroundMaterialIndex = StarMotifSettings.BackgroundMaterialIndex;
	}
	else if (FloorPattern == EChunkedFloorPattern::FlowerMotif)
	{
		BackgroundMaterialIndex = FlowerMotifSettings.BackgroundMaterialIndex;
	}
}

void AChunkedDestructibleDecorativeFloor::SetDecorativeFloorShape(
	EChunkedDecorativeFloorShape NewShape,
	FVector2D NewShapeSize,
	bool bPreserveCellState)
{
	OuterShape = NewShape;
	ShapeSize = FVector2D(
		FMath::Max(FMath::Abs(NewShapeSize.X), MinimumDecorativeFloorDimension),
		FMath::Max(FMath::Abs(NewShapeSize.Y), MinimumDecorativeFloorDimension));
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleDecorativeFloor::ConfigureDecorativeFloor(
	EChunkedDecorativeFloorShape NewShape,
	FVector2D NewShapeSize,
	FVector NewCellDimensions,
	FVector2D NewCellSpacing,
	bool bPreserveCellState)
{
	OuterShape = NewShape;
	ShapeSize = FVector2D(
		FMath::Max(FMath::Abs(NewShapeSize.X), MinimumDecorativeFloorDimension),
		FMath::Max(FMath::Abs(NewShapeSize.Y), MinimumDecorativeFloorDimension));
	CellDimensions = FVector(
		FMath::Max(FMath::Abs(NewCellDimensions.X), MinimumDecorativeFloorDimension),
		FMath::Max(FMath::Abs(NewCellDimensions.Y), MinimumDecorativeFloorDimension),
		FMath::Max(FMath::Abs(NewCellDimensions.Z), MinimumDecorativeFloorDimension));
	CellSpacing = FVector2D(
		FMath::Max(NewCellSpacing.X, 0.0),
		FMath::Max(NewCellSpacing.Y, 0.0));
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleDecorativeFloor::ConfigureStarShape(
	int32 NewPointCount,
	float NewInnerRadius,
	float NewRotationDegrees,
	bool bPreserveCellState)
{
	OuterShape = EChunkedDecorativeFloorShape::Star;
	StarPointCount = FMath::Clamp(NewPointCount, 3, 32);
	StarInnerRadius = FMath::Clamp(NewInnerRadius, 0.1f, 0.9f);
	StarRotationDegrees = NewRotationDegrees;
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleDecorativeFloor::ConfigureRoundedBlobShape(
	int32 NewLobeCount,
	float NewLobeStrength,
	float NewEdgeRoundness,
	int32 NewShapeSeed,
	bool bPreserveCellState)
{
	OuterShape = EChunkedDecorativeFloorShape::RoundedBlob;
	BlobLobeCount = FMath::Clamp(NewLobeCount, 3, 16);
	BlobLobeStrength = FMath::Clamp(NewLobeStrength, 0.0f, 0.45f);
	BlobEdgeRoundness = FMath::Clamp(NewEdgeRoundness, 0.0f, 1.0f);
	BlobShapeSeed = NewShapeSeed;
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleDecorativeFloor::ConfigureHeartMotif(
	bool bNewHeartMotifEnabled,
	FVector2D NewHeartSize,
	float NewInnerHeartScale,
	FVector2D NewHeartOffset,
	float NewHeartRotationDegrees)
{
	bUseHeartMotif = bNewHeartMotifEnabled;
	HeartMotifSettings.Size = FVector2D(
		FMath::Max(FMath::Abs(NewHeartSize.X), MinimumDecorativeFloorDimension),
		FMath::Max(FMath::Abs(NewHeartSize.Y), MinimumDecorativeFloorDimension));
	HeartMotifSettings.InnerScale = FMath::Clamp(NewInnerHeartScale, 0.05f, 0.9f);
	if (!HeartMotifSettings.bAutoCenter)
	{
		HeartMotifSettings.Offset = NewHeartOffset;
	}
	HeartMotifSettings.RotationDegrees = NewHeartRotationDegrees;
	if (bNewHeartMotifEnabled)
	{
		FloorPattern = EChunkedFloorPattern::HeartMotif;
	}
	else if (FloorPattern == EChunkedFloorPattern::HeartMotif)
	{
		FloorPattern = EChunkedFloorPattern::Solid;
	}
	bMotifSettingsMigrated = true;
	SyncLegacyFieldsFromMotifSettings();
	ApplyMaterialPattern();
}

void AChunkedDestructibleDecorativeFloor::ConfigureStarMotif(
	FVector2D NewStarSize,
	int32 NewPointCount,
	float NewInnerRadius,
	float NewInnerStarScale,
	FVector2D NewStarOffset,
	float NewStarRotationDegrees)
{
	StarMotifSettings.Size = FVector2D(
		FMath::Max(FMath::Abs(NewStarSize.X), MinimumDecorativeFloorDimension),
		FMath::Max(FMath::Abs(NewStarSize.Y), MinimumDecorativeFloorDimension));
	StarMotifSettings.PointCount = FMath::Clamp(NewPointCount, 3, 32);
	StarMotifSettings.InnerRadius = FMath::Clamp(NewInnerRadius, 0.1f, 0.9f);
	StarMotifSettings.InnerScale = FMath::Clamp(NewInnerStarScale, 0.05f, 0.9f);
	if (!StarMotifSettings.bAutoCenter)
	{
		StarMotifSettings.Offset = NewStarOffset;
	}
	StarMotifSettings.RotationDegrees = NewStarRotationDegrees;
	FloorPattern = EChunkedFloorPattern::StarMotif;
	bMotifSettingsMigrated = true;
	SyncLegacyFieldsFromMotifSettings();
	ApplyMaterialPattern();
}

void AChunkedDestructibleDecorativeFloor::ConfigureFlowerMotif(
	EChunkedFlowerType NewFlowerType,
	FVector2D NewFlowerSize,
	int32 NewPetalCount,
	FVector2D NewOffset,
	float NewRotationDegrees)
{
	FlowerMotifSettings.FlowerType = NewFlowerType;
	FlowerMotifSettings.FlowerSize = FVector2D(
		FMath::Max(FMath::Abs(NewFlowerSize.X), 50.0),
		FMath::Max(FMath::Abs(NewFlowerSize.Y), 50.0));
	if (NewFlowerType == EChunkedFlowerType::Daisy)
	{
		FlowerMotifSettings.PetalCount = FMath::Clamp(NewPetalCount, 3, 24);
	}
	else
	{
		FlowerMotifSettings.SimplePetalCount = FMath::Clamp(NewPetalCount, 3, 12);
	}
	if (!FlowerMotifSettings.bAutoCenter)
	{
		FlowerMotifSettings.Offset = NewOffset;
	}
	FlowerMotifSettings.RotationDegrees = NewRotationDegrees;
	FloorPattern = EChunkedFloorPattern::FlowerMotif;
	SyncLegacyFieldsFromMotifSettings();
	ApplyMaterialPattern();
}

bool AChunkedDestructibleDecorativeFloor::CalculateLayout()
{
	ShapeSize = FVector2D(
		FMath::Max(FMath::Abs(ShapeSize.X), MinimumDecorativeFloorDimension),
		FMath::Max(FMath::Abs(ShapeSize.Y), MinimumDecorativeFloorDimension));
	TriangleApexOffset = FMath::Clamp(TriangleApexOffset, -1.0f, 1.0f);
	if (OuterShape == EChunkedDecorativeFloorShape::Square ||
		OuterShape == EChunkedDecorativeFloorShape::Circle)
	{
		ShapeSize.Y = ShapeSize.X;
	}
	StarPointCount = FMath::Clamp(StarPointCount, 3, 32);
	StarInnerRadius = FMath::Clamp(StarInnerRadius, 0.1f, 0.9f);
	CircleEdgeSegments = FMath::Clamp(CircleEdgeSegments, 16, 512);
	BlobLobeCount = FMath::Clamp(BlobLobeCount, 3, 16);
	BlobLobeStrength = FMath::Clamp(BlobLobeStrength, 0.0f, 0.45f);
	BlobEdgeRoundness = FMath::Clamp(BlobEdgeRoundness, 0.0f, 1.0f);
	BlobOutlineSamplesPerLobe = FMath::Clamp(BlobOutlineSamplesPerLobe, 4, 32);
	BorderWidth = FMath::Max(BorderWidth, 0.0f);
	HeartSize = FVector2D(
		FMath::Max(FMath::Abs(HeartSize.X), MinimumDecorativeFloorDimension),
		FMath::Max(FMath::Abs(HeartSize.Y), MinimumDecorativeFloorDimension));
	InnerHeartScale = FMath::Clamp(InnerHeartScale, 0.05f, 0.9f);
	HeartCurveResolution = FMath::Clamp(HeartCurveResolution, 32, 256);
	MotifStarSize = FVector2D(
		FMath::Max(FMath::Abs(MotifStarSize.X), MinimumDecorativeFloorDimension),
		FMath::Max(FMath::Abs(MotifStarSize.Y), MinimumDecorativeFloorDimension));
	MotifStarPointCount = FMath::Clamp(MotifStarPointCount, 3, 32);
	MotifStarInnerRadius = FMath::Clamp(MotifStarInnerRadius, 0.1f, 0.9f);
	InnerStarScale = FMath::Clamp(InnerStarScale, 0.05f, 0.9f);
	FlowerMotifSettings.FlowerSize = FVector2D(
		FMath::Max(FMath::Abs(FlowerMotifSettings.FlowerSize.X), 50.0),
		FMath::Max(FMath::Abs(FlowerMotifSettings.FlowerSize.Y), 50.0));
	FlowerMotifSettings.PetalCount = FMath::Clamp(FlowerMotifSettings.PetalCount, 3, 24);
	FlowerMotifSettings.PetalLengthRatio = FMath::Clamp(FlowerMotifSettings.PetalLengthRatio, 0.15f, 0.75f);
	FlowerMotifSettings.PetalWidthRatio = FMath::Clamp(FlowerMotifSettings.PetalWidthRatio, 0.08f, 0.65f);
	FlowerMotifSettings.CenterRadiusRatio = FMath::Clamp(FlowerMotifSettings.CenterRadiusRatio, 0.05f, 0.55f);
	FlowerMotifSettings.LargeTypeScale = FMath::Clamp(FlowerMotifSettings.LargeTypeScale, 1.0f, 4.0f);
	FlowerMotifSettings.SimplePetalCount = FMath::Clamp(FlowerMotifSettings.SimplePetalCount, 3, 12);
	FlowerMotifSettings.SimplePetalLengthRatio = FMath::Clamp(FlowerMotifSettings.SimplePetalLengthRatio, 0.3f, 0.8f);
	FlowerMotifSettings.SimplePetalWidthRatio = FMath::Clamp(FlowerMotifSettings.SimplePetalWidthRatio, 0.2f, 0.8f);
	FlowerMotifSettings.SimpleCenterRadiusRatio = FMath::Clamp(FlowerMotifSettings.SimpleCenterRadiusRatio, 0.15f, 0.65f);
	FlowerMotifSettings.OutlineWidth = FMath::Max(FlowerMotifSettings.OutlineWidth, 0.0f);
	FlowerMotifSettings.CurveResolution = FMath::Clamp(FlowerMotifSettings.CurveResolution, 8, 64);
	AreaSize = ShapeSize;
	RebuildBlobCache();
	RebuildShapeOutline();
	CenterMotifsInShape();
	if (!Super::CalculateLayout())
	{
		CellGeometryCache.Reset();
		PatternSurfaceCaches.Reset();
		return false;
	}
	RebuildCellGeometryCache();
	return true;
}

void AChunkedDestructibleDecorativeFloor::RebuildBlobCache()
{
	CachedBlobRadii.Reset();
	const int32 AnchorCount = FMath::Clamp(BlobLobeCount, 3, 16) * 2;
	const float Strength = FMath::Clamp(BlobLobeStrength, 0.0f, 0.45f);
	FRandomStream Random(BlobShapeSeed);
	CachedBlobRadii.Reserve(AnchorCount);
	for (int32 AnchorIndex = 0; AnchorIndex < AnchorCount; ++AnchorIndex)
	{
		const bool bOuterAnchor = (AnchorIndex % 2) == 0;
		const float Variation = Random.FRand();
		const float Radius = bOuterAnchor
			? 1.0f - Strength * 0.12f * Variation
			: 1.0f - Strength * (0.60f + 0.40f * Variation);
		CachedBlobRadii.Add(FMath::Clamp(Radius, 0.5f, 1.0f));
	}
}

void AChunkedDestructibleDecorativeFloor::RebuildShapeOutline()
{
	CachedShapeOutline.Reset();
	CachedShapeTriangles.Reset();
	CachedShapeFanCenter = FVector2D::ZeroVector;

	const FVector2D HalfSize = ShapeSize * 0.5;
	switch (OuterShape)
	{
	case EChunkedDecorativeFloorShape::Triangle:
		CachedShapeOutline = {
			FVector2D(-HalfSize.X, -HalfSize.Y),
			FVector2D(HalfSize.X, -HalfSize.Y),
			FVector2D(FMath::Clamp(TriangleApexOffset, -1.0f, 1.0f) * HalfSize.X, HalfSize.Y)
		};
		CachedShapeFanCenter =
			(CachedShapeOutline[0] + CachedShapeOutline[1] + CachedShapeOutline[2]) / 3.0;
		break;

	case EChunkedDecorativeFloorShape::FivePointStar:
	case EChunkedDecorativeFloorShape::SixPointStar:
	case EChunkedDecorativeFloorShape::Star:
	{
		const int32 PointCount = OuterShape == EChunkedDecorativeFloorShape::FivePointStar
			? 5
			: (OuterShape == EChunkedDecorativeFloorShape::SixPointStar
				? 6
				: FMath::Clamp(StarPointCount, 3, 32));
		const float InnerRadius = FMath::Clamp(StarInnerRadius, 0.1f, 0.9f);
		const float RotationRadians = FMath::DegreesToRadians(StarRotationDegrees);
		CachedShapeOutline.Reserve(PointCount * 2);
		for (int32 Index = 0; Index < PointCount * 2; ++Index)
		{
			const float Radius = (Index & 1) == 0 ? 1.0f : InnerRadius;
			const float Angle = UE_HALF_PI + RotationRadians + Index * PI / PointCount;
			CachedShapeOutline.Add(FVector2D(
				FMath::Cos(Angle) * HalfSize.X * Radius,
				FMath::Sin(Angle) * HalfSize.Y * Radius));
		}
		break;
	}

	case EChunkedDecorativeFloorShape::Hexagon:
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const double Angle = UE_DOUBLE_TWO_PI * Index / 6.0;
			CachedShapeOutline.Add(FVector2D(FMath::Cos(Angle) * HalfSize.X,
				FMath::Sin(Angle) * HalfSize.Y / FMath::Sin(UE_DOUBLE_PI / 3.0)));
		}
		break;

	case EChunkedDecorativeFloorShape::Circle:
	{
		const int32 SegmentCount = FMath::Clamp(CircleEdgeSegments, 16, 512);
		CachedShapeOutline.Reserve(SegmentCount);
		for (int32 Index = 0; Index < SegmentCount; ++Index)
		{
			const double Angle = UE_TWO_PI * static_cast<double>(Index) / SegmentCount;
			CachedShapeOutline.Add(FVector2D(
				FMath::Cos(Angle) * HalfSize.X,
				FMath::Sin(Angle) * HalfSize.Y));
		}
		break;
	}

	case EChunkedDecorativeFloorShape::RoundedBlob:
	{
		const int32 SampleCount = FMath::Clamp(BlobLobeCount, 3, 16) *
			FMath::Clamp(BlobOutlineSamplesPerLobe, 4, 32);
		CachedShapeOutline.Reserve(SampleCount);
		for (int32 Index = 0; Index < SampleCount; ++Index)
		{
			const float Angle = UE_TWO_PI * static_cast<float>(Index) / SampleCount;
			const float Radius = SampleBlobRadius(Angle);
			CachedShapeOutline.Add(FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius));
		}

		FBox2D RawBounds(EForceInit::ForceInit);
		for (const FVector2D& Point : CachedShapeOutline)
		{
			RawBounds += Point;
		}
		const FVector2D RawSize = RawBounds.GetSize();
		const FVector2D RawCenter = RawBounds.GetCenter();
		const FVector2D Scale(
			ShapeSize.X / FMath::Max(RawSize.X, static_cast<double>(KINDA_SMALL_NUMBER)),
			ShapeSize.Y / FMath::Max(RawSize.Y, static_cast<double>(KINDA_SMALL_NUMBER)));
		for (FVector2D& Point : CachedShapeOutline)
		{
			Point = (Point - RawCenter) * Scale;
		}
		CachedShapeFanCenter = -RawCenter * Scale;
		break;
	}

	case EChunkedDecorativeFloorShape::Square:
	case EChunkedDecorativeFloorShape::Grid:
	default:
		CachedShapeOutline = {
			FVector2D(-HalfSize.X, -HalfSize.Y),
			FVector2D(HalfSize.X, -HalfSize.Y),
			FVector2D(HalfSize.X, HalfSize.Y),
			FVector2D(-HalfSize.X, HalfSize.Y)
		};
		break;
	}

	if (SignedPolygonArea(CachedShapeOutline) < 0.0)
	{
		Algo::Reverse(CachedShapeOutline);
	}

	CachedShapeTriangles.Reserve(CachedShapeOutline.Num());
	for (int32 Index = 0; Index < CachedShapeOutline.Num(); ++Index)
	{
		FShapeTriangle& Triangle = CachedShapeTriangles.Emplace_GetRef();
		Triangle.A = CachedShapeFanCenter;
		Triangle.B = CachedShapeOutline[Index];
		Triangle.C = CachedShapeOutline[(Index + 1) % CachedShapeOutline.Num()];
		Triangle.Bounds = FBox2D(EForceInit::ForceInit);
		Triangle.Bounds += Triangle.A;
		Triangle.Bounds += Triangle.B;
		Triangle.Bounds += Triangle.C;
	}
}

void AChunkedDestructibleDecorativeFloor::CenterMotifsInShape()
{
	FVector2D ShapeBoundsCenter = FVector2D::ZeroVector;
	if (!CachedShapeOutline.IsEmpty())
	{
		FBox2D ShapeBounds(EForceInit::ForceInit);
		for (const FVector2D& Point : CachedShapeOutline)
		{
			ShapeBounds += Point;
		}
		ShapeBoundsCenter = ShapeBounds.GetCenter();
	}

	if (HeartMotifSettings.bAutoCenter)
	{
		HeartMotifSettings.Offset = ShapeBoundsCenter;
	}
	if (StarMotifSettings.bAutoCenter)
	{
		StarMotifSettings.Offset = ShapeBoundsCenter;
	}
	if (FlowerMotifSettings.bAutoCenter)
	{
		FlowerMotifSettings.Offset = ShapeBoundsCenter;
	}
	HeartOffset = HeartMotifSettings.Offset;
	MotifStarOffset = StarMotifSettings.Offset;
}

float AChunkedDestructibleDecorativeFloor::SampleBlobRadius(float Angle) const
{
	if (CachedBlobRadii.IsEmpty())
	{
		return 1.0f;
	}

	float WrappedAngle = FMath::Fmod(Angle + UE_TWO_PI, UE_TWO_PI);
	if (WrappedAngle < 0.0f)
	{
		WrappedAngle += UE_TWO_PI;
	}
	const float AnchorPosition = WrappedAngle / UE_TWO_PI * CachedBlobRadii.Num();
	const int32 AnchorIndex = FMath::FloorToInt(AnchorPosition);
	const float Alpha = AnchorPosition - AnchorIndex;
	auto RadiusAt = [this](int32 Index)
	{
		const int32 WrappedIndex = (Index % CachedBlobRadii.Num() + CachedBlobRadii.Num()) % CachedBlobRadii.Num();
		return CachedBlobRadii[WrappedIndex];
	};

	const float Radius0 = RadiusAt(AnchorIndex - 1);
	const float Radius1 = RadiusAt(AnchorIndex);
	const float Radius2 = RadiusAt(AnchorIndex + 1);
	const float Radius3 = RadiusAt(AnchorIndex + 2);
	const float LinearRadius = FMath::Lerp(Radius1, Radius2, Alpha);
	const float AlphaSquared = Alpha * Alpha;
	const float AlphaCubed = AlphaSquared * Alpha;
	const float SplineRadius = 0.5f * (
		2.0f * Radius1 +
		(-Radius0 + Radius2) * Alpha +
		(2.0f * Radius0 - 5.0f * Radius1 + 4.0f * Radius2 - Radius3) * AlphaSquared +
		(-Radius0 + 3.0f * Radius1 - 3.0f * Radius2 + Radius3) * AlphaCubed);
	return FMath::Clamp(
		FMath::Lerp(LinearRadius, SplineRadius, FMath::Clamp(BlobEdgeRoundness, 0.0f, 1.0f)),
		0.45f,
		1.0f);
}

bool AChunkedDestructibleDecorativeFloor::IsPointInsidePolygon(
	const FVector2D& Point,
	const TArray<FVector2D>& Polygon) const
{
	if (Polygon.Num() < 3)
	{
		return false;
	}

	bool bInside = false;
	for (int32 Index = 0, PreviousIndex = Polygon.Num() - 1; Index < Polygon.Num(); PreviousIndex = Index++)
	{
		const FVector2D& A = Polygon[Index];
		const FVector2D& B = Polygon[PreviousIndex];
		const FVector2D Segment = B - A;
		const float SegmentLengthSquared = Segment.SizeSquared();
		if (SegmentLengthSquared > ShapeTolerance)
		{
			const float Alpha = FMath::Clamp(
				FVector2D::DotProduct(Point - A, Segment) / SegmentLengthSquared,
				0.0f,
				1.0f);
			if ((Point - (A + Segment * Alpha)).SizeSquared() <= ShapeTolerance)
			{
				return true;
			}
		}

		if ((A.Y > Point.Y) != (B.Y > Point.Y))
		{
			const double IntersectionX = A.X +
				(Point.Y - A.Y) * (B.X - A.X) / (B.Y - A.Y);
			if (Point.X < IntersectionX)
			{
				bInside = !bInside;
			}
		}
	}
	return bInside;
}

bool AChunkedDestructibleDecorativeFloor::IsPointInsideGeneratedShape(
	const FVector2D& Point,
	double Tolerance) const
{
	if (CachedShapeOutline.Num() < 3)
	{
		return false;
	}

	bool bInside = false;
	for (int32 Index = 0; Index < CachedShapeOutline.Num(); ++Index)
	{
		const FVector2D& A = CachedShapeOutline[Index];
		const FVector2D& B = CachedShapeOutline[(Index + 1) % CachedShapeOutline.Num()];
		if (IsPointOnSegment(Point, A, B, Tolerance))
		{
			return true;
		}
		if ((A.Y > Point.Y) != (B.Y > Point.Y))
		{
			const double IntersectionX = A.X +
				(Point.Y - A.Y) * (B.X - A.X) / (B.Y - A.Y);
			if (Point.X < IntersectionX)
			{
				bInside = !bInside;
			}
		}
	}
	return bInside;
}

void AChunkedDestructibleDecorativeFloor::GetCellBounds(
	FIntPoint Cell,
	double& OutMinX,
	double& OutMinY,
	double& OutMaxX,
	double& OutMaxY) const
{
	const FVector Center = GetCellLocalCenter(Cell);
	OutMinX = Center.X - EffectiveCellDimensions.X * 0.5;
	OutMaxX = Center.X + EffectiveCellDimensions.X * 0.5;
	OutMinY = Center.Y - EffectiveCellDimensions.Y * 0.5;
	OutMaxY = Center.Y + EffectiveCellDimensions.Y * 0.5;
}

bool AChunkedDestructibleDecorativeFloor::DoesShapeOutlineIntersectCell(FIntPoint Cell) const
{
	double MinX;
	double MinY;
	double MaxX;
	double MaxY;
	GetCellBounds(Cell, MinX, MinY, MaxX, MaxY);
	for (int32 Index = 0; Index < CachedShapeOutline.Num(); ++Index)
	{
		FVector2D ClippedStart;
		FVector2D ClippedEnd;
		if (ClipSegmentToRectangle(
			CachedShapeOutline[Index],
			CachedShapeOutline[(Index + 1) % CachedShapeOutline.Num()],
			MinX, MinY, MaxX, MaxY,
			ClippedStart, ClippedEnd))
		{
			return true;
		}
	}
	return false;
}

bool AChunkedDestructibleDecorativeFloor::ComputeCellFullyInsideGeneratedShape(FIntPoint Cell) const
{
	double MinX;
	double MinY;
	double MaxX;
	double MaxY;
	GetCellBounds(Cell, MinX, MinY, MaxX, MaxY);
	return IsPointInsideGeneratedShape(FVector2D(MinX, MinY)) &&
		IsPointInsideGeneratedShape(FVector2D(MaxX, MinY)) &&
		IsPointInsideGeneratedShape(FVector2D(MaxX, MaxY)) &&
		IsPointInsideGeneratedShape(FVector2D(MinX, MaxY)) &&
		!DoesShapeOutlineIntersectCell(Cell);
}

bool AChunkedDestructibleDecorativeFloor::ComputeClippedCellFragments(
	FIntPoint Cell,
	TArray<TArray<FVector2D>>& OutFragments) const
{
	OutFragments.Reset();
	if (!IsValidCell(Cell))
	{
		return false;
	}

	double MinX;
	double MinY;
	double MaxX;
	double MaxY;
	GetCellBounds(Cell, MinX, MinY, MaxX, MaxY);
	const double BoundaryValues[4] = { MinX, MaxX, MinY, MaxY };
	const double MinimumArea = FMath::Max(
		EffectiveCellDimensions.X * EffectiveCellDimensions.Y * 0.00001,
		0.001);

	for (const FShapeTriangle& Triangle : CachedShapeTriangles)
	{
		if (Triangle.Bounds.Max.X < MinX - ShapePolygonTolerance ||
			Triangle.Bounds.Min.X > MaxX + ShapePolygonTolerance ||
			Triangle.Bounds.Max.Y < MinY - ShapePolygonTolerance ||
			Triangle.Bounds.Min.Y > MaxY + ShapePolygonTolerance)
		{
			continue;
		}

		TArray<FVector2D> Fragment = { Triangle.A, Triangle.B, Triangle.C };
		for (int32 BoundaryIndex = 0; BoundaryIndex < 4 && !Fragment.IsEmpty(); ++BoundaryIndex)
		{
			ClipPolygonAgainstBoundary(Fragment, BoundaryIndex, BoundaryValues[BoundaryIndex]);
		}
		RemoveDuplicatePolygonPoints(Fragment);
		if (Fragment.Num() >= 3 && FMath::Abs(SignedPolygonArea(Fragment)) >= MinimumArea)
		{
			OutFragments.Add(MoveTemp(Fragment));
		}
	}
	return !OutFragments.IsEmpty();
}

void AChunkedDestructibleDecorativeFloor::RebuildCellGeometryCache()
{
	CellGeometryCache.SetNum(CellCount.X * CellCount.Y);
	PatternSurfaceCaches.Reset();
	for (int32 Y = 0; Y < CellCount.Y; ++Y)
	{
		for (int32 X = 0; X < CellCount.X; ++X)
		{
			const FIntPoint Cell(X, Y);
			FDecorativeCellGeometryCache& Cache = CellGeometryCache[ToCellIndex(Cell)];
			Cache.bBelongsToShape = ComputeClippedCellFragments(Cell, Cache.Fragments);
			Cache.bFullyInside = Cache.bBelongsToShape && ComputeCellFullyInsideGeneratedShape(Cell);
			Cache.OutlineSegments.Reset();
			if (!Cache.bBelongsToShape || Cache.bFullyInside)
			{
				continue;
			}

			double MinX;
			double MinY;
			double MaxX;
			double MaxY;
			GetCellBounds(Cell, MinX, MinY, MaxX, MaxY);
			for (int32 EdgeIndex = 0; EdgeIndex < CachedShapeOutline.Num(); ++EdgeIndex)
			{
				FVector2D ClippedStart;
				FVector2D ClippedEnd;
				if (ClipSegmentToRectangle(
					CachedShapeOutline[EdgeIndex],
					CachedShapeOutline[(EdgeIndex + 1) % CachedShapeOutline.Num()],
					MinX, MinY, MaxX, MaxY,
					ClippedStart, ClippedEnd))
				{
					FShapeOutlineSegment& Segment = Cache.OutlineSegments.Emplace_GetRef();
					Segment.A = ClippedStart;
					Segment.B = ClippedEnd;
				}
			}
		}
	}
}

bool AChunkedDestructibleDecorativeFloor::IsCellFullyInsideGeneratedShape(FIntPoint Cell) const
{
	const int32 CellIndex = IsValidCell(Cell) ? ToCellIndex(Cell) : INDEX_NONE;
	return CellGeometryCache.IsValidIndex(CellIndex)
		? CellGeometryCache[CellIndex].bFullyInside
		: ComputeCellFullyInsideGeneratedShape(Cell);
}

bool AChunkedDestructibleDecorativeFloor::GetClippedCellFragments(
	FIntPoint Cell,
	TArray<TArray<FVector2D>>& OutFragments) const
{
	const int32 CellIndex = IsValidCell(Cell) ? ToCellIndex(Cell) : INDEX_NONE;
	if (CellGeometryCache.IsValidIndex(CellIndex))
	{
		OutFragments = CellGeometryCache[CellIndex].Fragments;
		return CellGeometryCache[CellIndex].bBelongsToShape;
	}
	return ComputeClippedCellFragments(Cell, OutFragments);
}

bool AChunkedDestructibleDecorativeFloor::IsPointInsideNormalizedShape(const FVector2D& Point) const
{
	switch (OuterShape)
	{
	case EChunkedDecorativeFloorShape::Triangle:
	{
		const TArray<FVector2D> Triangle = {
			FVector2D(-1.0, -1.0),
			FVector2D(1.0, -1.0),
			FVector2D(FMath::Clamp(TriangleApexOffset, -1.0f, 1.0f), 1.0)
		};
		return IsPointInsidePolygon(Point, Triangle);
	}

	case EChunkedDecorativeFloorShape::FivePointStar:
	case EChunkedDecorativeFloorShape::SixPointStar:
	case EChunkedDecorativeFloorShape::Star:
	{
		const int32 PointCount = OuterShape == EChunkedDecorativeFloorShape::FivePointStar
			? 5
			: (OuterShape == EChunkedDecorativeFloorShape::SixPointStar
				? 6
				: FMath::Clamp(StarPointCount, 3, 32));
		const float SafeInnerRadius = FMath::Clamp(StarInnerRadius, 0.1f, 0.9f);
		const float RotationRadians = FMath::DegreesToRadians(StarRotationDegrees);
		TArray<FVector2D> Star;
		Star.Reserve(PointCount * 2);
		for (int32 Index = 0; Index < PointCount * 2; ++Index)
		{
			const float Radius = (Index % 2) == 0 ? 1.0f : SafeInnerRadius;
			const float Angle = UE_HALF_PI + RotationRadians + Index * PI / PointCount;
			Star.Add(FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius));
		}
		return IsPointInsidePolygon(Point, Star);
	}

	case EChunkedDecorativeFloorShape::Circle:
		return Point.SizeSquared() <= 1.0f + ShapeTolerance;

	case EChunkedDecorativeFloorShape::RoundedBlob:
	{
		const float Distance = static_cast<float>(Point.Size());
		if (Distance <= ShapeTolerance)
		{
			return true;
		}
		return Distance <= SampleBlobRadius(FMath::Atan2(Point.Y, Point.X)) + ShapeTolerance;
	}

	case EChunkedDecorativeFloorShape::Square:
	case EChunkedDecorativeFloorShape::Grid:
	default:
		return FMath::Abs(Point.X) <= 1.0f + ShapeTolerance &&
			FMath::Abs(Point.Y) <= 1.0f + ShapeTolerance;
	}
}

bool AChunkedDestructibleDecorativeFloor::IsPointInsideOuterShape(
	const FVector2D& LocalPoint,
	float Inset) const
{
	const FVector2D OriginalHalfSize = ShapeSize * 0.5;
	const FVector2D InsetHalfSize = OriginalHalfSize - FVector2D(FMath::Max(Inset, 0.0f));
	if (InsetHalfSize.X <= ShapeTolerance || InsetHalfSize.Y <= ShapeTolerance ||
		OriginalHalfSize.X <= ShapeTolerance || OriginalHalfSize.Y <= ShapeTolerance)
	{
		return false;
	}

	const FVector2D Scale(
		InsetHalfSize.X / OriginalHalfSize.X,
		InsetHalfSize.Y / OriginalHalfSize.Y);
	return IsPointInsideGeneratedShape(FVector2D(LocalPoint.X / Scale.X, LocalPoint.Y / Scale.Y));
}

bool AChunkedDestructibleDecorativeFloor::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{
	TArray<TArray<FVector2D>> Fragments;
	return GetClippedCellFragments(Cell, Fragments);
}

int32 AChunkedDestructibleDecorativeFloor::ClampMaterialIndex(int32 MaterialIndex) const
{
	return FloorMaterials.IsEmpty() ? 0 : FMath::Clamp(MaterialIndex, 0, FloorMaterials.Num() - 1);
}

int32 AChunkedDestructibleDecorativeFloor::EvaluatePatternMaterial(FIntPoint Cell) const
{
	const FVector Center3D = GetCellLocalCenter(Cell);
	const FVector2D Center(Center3D.X, Center3D.Y);

	if (bUseBrightBorder &&
		(!IsPointInsideOuterShape(Center) || !IsPointInsideOuterShape(Center, BorderWidth)))
	{
		return ClampMaterialIndex(BorderMaterialIndex);
	}

	// Smooth overlay motifs use a neutral backing to avoid a blocky cell-colored fringe.
	if (FloorPattern == EChunkedFloorPattern::FlowerMotif)
	{
		return ClampMaterialIndex(FlowerMotifSettings.BackgroundMaterialIndex);
	}
	if (FloorPattern == EChunkedFloorPattern::HeartMotif ||
		FloorPattern == EChunkedFloorPattern::StarMotif)
	{
		return ClampMaterialIndex(BackgroundMaterialIndex);
	}

	return Super::EvaluatePatternMaterial(Cell);
}

void AChunkedDestructibleDecorativeFloor::AppendPolygonHorizontalFace(
	FDynamicMesh3& Mesh,
	const TArray<FVector2D>& Polygon,
	bool bTopFace,
	int32 MaterialIndex) const
{
	if (Polygon.Num() < 3)
	{
		return;
	}

	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
	const double Z = bTopFace ? 0.0 : -EffectiveCellDimensions.Z;
	const FVector3f Normal = bTopFace ? FVector3f::UnitZ() : -FVector3f::UnitZ();
	for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
	{
		const FVector2D& PointA = Polygon[0];
		const FVector2D& PointB = bTopFace ? Polygon[Index] : Polygon[Index + 1];
		const FVector2D& PointC = bTopFace ? Polygon[Index + 1] : Polygon[Index];
		AppendMeshTriangle(
			Mesh,
			FVector3d(PointA.X, PointA.Y, Z),
			FVector3d(PointB.X, PointB.Y, Z),
			FVector3d(PointC.X, PointC.Y, Z),
			Normal,
			MaterialIndex,
			FVector2f(PointA.X / SafeUVScale, PointA.Y / SafeUVScale),
			FVector2f(PointB.X / SafeUVScale, PointB.Y / SafeUVScale),
			FVector2f(PointC.X / SafeUVScale, PointC.Y / SafeUVScale));
	}
}

void AChunkedDestructibleDecorativeFloor::AppendGreedyFullHorizontalFaces(
	FDynamicMesh3& Mesh,
	int32 MinCellX,
	int32 MinCellY,
	int32 MaxCellX,
	int32 MaxCellY,
	bool bTopFace) const
{
	const int32 Width = MaxCellX - MinCellX;
	const int32 Height = MaxCellY - MinCellY;
	TBitArray<> Visited(false, Width * Height);
	const bool bCanMergeX = CellSpacing.X <= ExactShapeSpacingTolerance;
	const bool bCanMergeY = CellSpacing.Y <= ExactShapeSpacingTolerance;
	auto LocalIndex = [Width, MinCellX, MinCellY](int32 X, int32 Y)
	{
		return (X - MinCellX) + (Y - MinCellY) * Width;
	};
	auto IsMergeCandidate = [this](int32 X, int32 Y, int32 MaterialIndex)
	{
		const FIntPoint Cell(X, Y);
		if (!IsValidCell(Cell))
		{
			return false;
		}
		const FFloorCellData& Data = Cells[ToCellIndex(Cell)];
		return Data.bExists &&
			Data.MaterialIndex == MaterialIndex &&
			IsCellFullyInsideGeneratedShape(Cell);
	};

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const int32 VisitedIndex = LocalIndex(X, Y);
			if (Visited[VisitedIndex])
			{
				continue;
			}
			const FIntPoint StartCell(X, Y);
			const FFloorCellData& StartData = Cells[ToCellIndex(StartCell)];
			if (!StartData.bExists || !IsCellFullyInsideGeneratedShape(StartCell))
			{
				Visited[VisitedIndex] = true;
				continue;
			}

			const int32 MaterialIndex = StartData.MaterialIndex;
			int32 MergeWidth = 1;
			if (bCanMergeX)
			{
				while (X + MergeWidth < MaxCellX &&
					!Visited[LocalIndex(X + MergeWidth, Y)] &&
					IsMergeCandidate(X + MergeWidth, Y, MaterialIndex))
				{
					++MergeWidth;
				}
			}

			int32 MergeHeight = 1;
			if (bCanMergeY)
			{
				bool bCanGrow = true;
				while (Y + MergeHeight < MaxCellY && bCanGrow)
				{
					for (int32 OffsetX = 0; OffsetX < MergeWidth; ++OffsetX)
					{
						const int32 TestX = X + OffsetX;
						const int32 TestY = Y + MergeHeight;
						if (Visited[LocalIndex(TestX, TestY)] ||
							!IsMergeCandidate(TestX, TestY, MaterialIndex))
						{
							bCanGrow = false;
							break;
						}
					}
					if (bCanGrow)
					{
						++MergeHeight;
					}
				}
			}

			for (int32 OffsetY = 0; OffsetY < MergeHeight; ++OffsetY)
			{
				for (int32 OffsetX = 0; OffsetX < MergeWidth; ++OffsetX)
				{
					Visited[LocalIndex(X + OffsetX, Y + OffsetY)] = true;
				}
			}

			double MinX;
			double MinY;
			double IgnoredMaxX;
			double IgnoredMaxY;
			GetCellBounds(StartCell, MinX, MinY, IgnoredMaxX, IgnoredMaxY);
			double IgnoredMinX;
			double IgnoredMinY;
			double MaxX;
			double MaxY;
			GetCellBounds(
				FIntPoint(X + MergeWidth - 1, Y + MergeHeight - 1),
				IgnoredMinX, IgnoredMinY, MaxX, MaxY);
			const double Z = bTopFace ? 0.0 : -EffectiveCellDimensions.Z;
			if (bTopFace)
			{
				AppendMeshQuad(
					Mesh,
					FVector3d(MinX, MinY, Z), FVector3d(MaxX, MinY, Z),
					FVector3d(MaxX, MaxY, Z), FVector3d(MinX, MaxY, Z),
					FVector3f::UnitZ(), MaterialIndex,
					static_cast<float>(MaxX - MinX), static_cast<float>(MaxY - MinY), UVWorldUnits);
			}
			else
			{
				AppendMeshQuad(
					Mesh,
					FVector3d(MinX, MaxY, Z), FVector3d(MaxX, MaxY, Z),
					FVector3d(MaxX, MinY, Z), FVector3d(MinX, MinY, Z),
					-FVector3f::UnitZ(), MaterialIndex,
					static_cast<float>(MaxX - MinX), static_cast<float>(MaxY - MinY), UVWorldUnits);
			}
		}
	}
}

void AChunkedDestructibleDecorativeFloor::BuildChunkMesh(
	int32 ChunkIndex,
	UE::Geometry::FDynamicMesh3& OutMesh) const
{
	OutMesh.EnableVertexNormals(FVector3f::UnitZ());
	OutMesh.EnableAttributes();
	OutMesh.Attributes()->SetNumUVLayers(1);
	OutMesh.Attributes()->EnableMaterialID();

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	const int32 ChunkX = ChunkIndex % ChunkCount.X;
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinCellX = ChunkX * SafeChunkSide;
	const int32 MinCellY = ChunkY * SafeChunkSide;
	const int32 MaxCellX = FMath::Min(MinCellX + SafeChunkSide, CellCount.X);
	const int32 MaxCellY = FMath::Min(MinCellY + SafeChunkSide, CellCount.Y);

	AppendGreedyFullHorizontalFaces(OutMesh, MinCellX, MinCellY, MaxCellX, MaxCellY, true);
	if (bGenerateBottomFaces)
	{
		AppendGreedyFullHorizontalFaces(OutMesh, MinCellX, MinCellY, MaxCellX, MaxCellY, false);
	}

	auto CellExists = [this](int32 X, int32 Y)
	{
		const FIntPoint Cell(X, Y);
		return IsValidCell(Cell) && Cells[ToCellIndex(Cell)].bExists;
	};
	auto IsShapeBoundarySegment = [this](const FVector2D& A, const FVector2D& B)
	{
		for (int32 Index = 0; Index < CachedShapeOutline.Num(); ++Index)
		{
			const FVector2D& Start = CachedShapeOutline[Index];
			const FVector2D& End = CachedShapeOutline[(Index + 1) % CachedShapeOutline.Num()];
			if (IsPointOnSegment(A, Start, End, ShapePolygonTolerance) &&
				IsPointOnSegment(B, Start, End, ShapePolygonTolerance))
			{
				return true;
			}
		}
		return false;
	};
	auto AppendWall = [this, &OutMesh](
		const FVector2D& A,
		const FVector2D& B,
		int32 MaterialIndex,
		bool bOuterBoundary)
	{
		const FVector2D Edge = B - A;
		const float EdgeLength = static_cast<float>(Edge.Size());
		if (EdgeLength <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		const FVector2D Outward2D(Edge.Y, -Edge.X);
		const FVector3f Normal = FVector3f(
			static_cast<float>(Outward2D.X),
			static_cast<float>(Outward2D.Y),
			0.0f).GetSafeNormal();
		const double BottomZ = -EffectiveCellDimensions.Z;
		AppendFloorSideQuad(
			OutMesh,
			FVector3d(B.X, B.Y, BottomZ), FVector3d(B.X, B.Y, 0.0),
			FVector3d(A.X, A.Y, 0.0), FVector3d(A.X, A.Y, BottomZ),
			Normal, MaterialIndex, EdgeLength,
			static_cast<float>(EffectiveCellDimensions.Z), bOuterBoundary);
	};

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const FIntPoint Cell(X, Y);
			const FFloorCellData& CellData = Cells[ToCellIndex(Cell)];
			if (!CellData.bExists)
			{
				continue;
			}

			const bool bFullCell = IsCellFullyInsideGeneratedShape(Cell);
			double CellMinX;
			double CellMinY;
			double CellMaxX;
			double CellMaxY;
			GetCellBounds(Cell, CellMinX, CellMinY, CellMaxX, CellMaxY);

			TArray<TArray<FVector2D>> Fragments;
			if (bFullCell)
			{
				Fragments.Add({
					FVector2D(CellMinX, CellMinY), FVector2D(CellMaxX, CellMinY),
					FVector2D(CellMaxX, CellMaxY), FVector2D(CellMinX, CellMaxY)
				});
			}
			else if (!GetClippedCellFragments(Cell, Fragments))
			{
				continue;
			}

			if (!bFullCell)
			{
				for (const TArray<FVector2D>& Fragment : Fragments)
				{
					AppendPolygonHorizontalFace(OutMesh, Fragment, true, CellData.MaterialIndex);
					if (bGenerateBottomFaces)
					{
						AppendPolygonHorizontalFace(OutMesh, Fragment, false, CellData.MaterialIndex);
					}
				}
			}

			// Grid-cut walls are needed for gaps and destroyed neighbours. Edges that already belong
			// to the exact silhouette are handled once by the outline pass below.
			for (const TArray<FVector2D>& Fragment : Fragments)
			{
				for (int32 EdgeIndex = 0; EdgeIndex < Fragment.Num(); ++EdgeIndex)
				{
					const FVector2D& A = Fragment[EdgeIndex];
					const FVector2D& B = Fragment[(EdgeIndex + 1) % Fragment.Num()];
					bool bExposedGridEdge = false;
					if (FMath::IsNearlyEqual(A.X, CellMinX, ShapePolygonTolerance) &&
						FMath::IsNearlyEqual(B.X, CellMinX, ShapePolygonTolerance))
					{
						bExposedGridEdge = CellSpacing.X > ExactShapeSpacingTolerance || !CellExists(X - 1, Y);
					}
					else if (FMath::IsNearlyEqual(A.X, CellMaxX, ShapePolygonTolerance) &&
						FMath::IsNearlyEqual(B.X, CellMaxX, ShapePolygonTolerance))
					{
						bExposedGridEdge = CellSpacing.X > ExactShapeSpacingTolerance || !CellExists(X + 1, Y);
					}
					else if (FMath::IsNearlyEqual(A.Y, CellMinY, ShapePolygonTolerance) &&
						FMath::IsNearlyEqual(B.Y, CellMinY, ShapePolygonTolerance))
					{
						bExposedGridEdge = CellSpacing.Y > ExactShapeSpacingTolerance || !CellExists(X, Y - 1);
					}
					else if (FMath::IsNearlyEqual(A.Y, CellMaxY, ShapePolygonTolerance) &&
						FMath::IsNearlyEqual(B.Y, CellMaxY, ShapePolygonTolerance))
					{
						bExposedGridEdge = CellSpacing.Y > ExactShapeSpacingTolerance || !CellExists(X, Y + 1);
					}
					if (bExposedGridEdge && !IsShapeBoundarySegment(A, B))
					{
						AppendWall(A, B, CellData.MaterialIndex, false);
					}
				}
			}

			const int32 CellIndex = ToCellIndex(Cell);
			if (!bFullCell && CellGeometryCache.IsValidIndex(CellIndex))
			{
				for (const FShapeOutlineSegment& Segment : CellGeometryCache[CellIndex].OutlineSegments)
				{
					AppendWall(Segment.A, Segment.B, CellData.MaterialIndex, true);
				}
			}
		}
	}

	AppendDecorativeSurfacePatterns(ChunkIndex, OutMesh);
}

void AChunkedDestructibleDecorativeFloor::AppendDecorativeSurfacePatterns(
	int32 ChunkIndex, FDynamicMesh3& OutMesh) const
{
	if (FloorPattern == EChunkedFloorPattern::HypnoticSpiral)
	{
		AppendHypnoticSpiralSurface2D(
			OutMesh,
			ChunkIndex,
			FVector2D::ZeroVector,
			(ShapeSize * 0.5).Size(),
			[this](FIntPoint Cell, TArray<TArray<FVector2D>>& OutPolygons)
			{
				OutPolygons.Reset();
				return IsValidCell(Cell) &&
					Cells[ToCellIndex(Cell)].bExists &&
					GetClippedCellFragments(Cell, OutPolygons);
			},
			[this, &OutMesh](FIntPoint, const FVector2D& A, const FVector2D& B, const FVector2D& C, int32 MaterialIndex)
			{
				const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
				AppendMeshTriangle(
					OutMesh,
					FVector3d(A.X, A.Y, HeartSurfaceLift),
					FVector3d(B.X, B.Y, HeartSurfaceLift),
					FVector3d(C.X, C.Y, HeartSurfaceLift),
					FVector3f::UnitZ(),
					MaterialIndex,
					FVector2f(A.X / SafeUVScale, A.Y / SafeUVScale),
					FVector2f(B.X / SafeUVScale, B.Y / SafeUVScale),
					FVector2f(C.X / SafeUVScale, C.Y / SafeUVScale));
			});
	}

	if (FloorMaterials.IsEmpty())
	{
		return;
	}

	if (FloorPattern == EChunkedFloorPattern::HeartMotif)
	{
		AppendHeartSurface(
			OutMesh,
			ChunkIndex,
			1.0f,
			ClampMaterialIndex(HeartAccentMaterialIndex),
			HeartSurfaceLift);
		AppendHeartSurface(
			OutMesh,
			ChunkIndex,
			FMath::Clamp(InnerHeartScale, 0.05f, 0.9f),
			ClampMaterialIndex(InnerHeartMaterialIndex),
			HeartSurfaceLift + HeartLayerSeparation);
	}

	if (FloorPattern == EChunkedFloorPattern::StarMotif)
	{
		AppendStarSurface(
			OutMesh,
			ChunkIndex,
			1.0f,
			ClampMaterialIndex(StarAccentMaterialIndex),
			HeartSurfaceLift);
		AppendStarSurface(
			OutMesh,
			ChunkIndex,
			FMath::Clamp(InnerStarScale, 0.05f, 0.9f),
			ClampMaterialIndex(InnerStarMaterialIndex),
			HeartSurfaceLift + HeartLayerSeparation);
	}

	if (FloorPattern == EChunkedFloorPattern::FlowerMotif)
	{
		AppendFlowerPattern(OutMesh, ChunkIndex);
	}
}

void AChunkedDestructibleDecorativeFloor::BuildChunkSimpleCollision(
	int32 ChunkIndex,
	FKAggregateGeom& OutCollision) const
{
	OutCollision.EmptyElements();
	if (!bGenerateCollision || ChunkCount.X <= 0)
	{
		return;
	}

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	const int32 ChunkX = ChunkIndex % ChunkCount.X;
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinCellX = ChunkX * SafeChunkSide;
	const int32 MinCellY = ChunkY * SafeChunkSide;
	const int32 MaxCellX = FMath::Min(MinCellX + SafeChunkSide, CellCount.X);
	const int32 MaxCellY = FMath::Min(MinCellY + SafeChunkSide, CellCount.Y);
	const int32 Width = MaxCellX - MinCellX;
	const int32 Height = MaxCellY - MinCellY;
	TBitArray<> Visited(false, Width * Height);
	const bool bCanMergeX = CellSpacing.X <= ExactShapeSpacingTolerance;
	const bool bCanMergeY = CellSpacing.Y <= ExactShapeSpacingTolerance;
	auto LocalIndex = [Width, MinCellX, MinCellY](int32 X, int32 Y)
	{
		return (X - MinCellX) + (Y - MinCellY) * Width;
	};
	auto IsFullExistingCell = [this](int32 X, int32 Y)
	{
		const FIntPoint Cell(X, Y);
		return IsValidCell(Cell) &&
			Cells[ToCellIndex(Cell)].bExists &&
			IsCellFullyInsideGeneratedShape(Cell);
	};

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const int32 StartIndex = LocalIndex(X, Y);
			if (Visited[StartIndex])
			{
				continue;
			}
			if (!IsFullExistingCell(X, Y))
			{
				Visited[StartIndex] = true;
				continue;
			}

			int32 MergeWidth = 1;
			if (bCanMergeX)
			{
				while (X + MergeWidth < MaxCellX &&
					!Visited[LocalIndex(X + MergeWidth, Y)] &&
					IsFullExistingCell(X + MergeWidth, Y))
				{
					++MergeWidth;
				}
			}

			int32 MergeHeight = 1;
			if (bCanMergeY)
			{
				bool bCanGrow = true;
				while (Y + MergeHeight < MaxCellY && bCanGrow)
				{
					for (int32 OffsetX = 0; OffsetX < MergeWidth; ++OffsetX)
					{
						const int32 TestX = X + OffsetX;
						const int32 TestY = Y + MergeHeight;
						if (Visited[LocalIndex(TestX, TestY)] || !IsFullExistingCell(TestX, TestY))
						{
							bCanGrow = false;
							break;
						}
					}
					if (bCanGrow)
					{
						++MergeHeight;
					}
				}
			}

			for (int32 OffsetY = 0; OffsetY < MergeHeight; ++OffsetY)
			{
				for (int32 OffsetX = 0; OffsetX < MergeWidth; ++OffsetX)
				{
					Visited[LocalIndex(X + OffsetX, Y + OffsetY)] = true;
				}
			}

			double BoxMinX;
			double BoxMinY;
			double IgnoredMaxX;
			double IgnoredMaxY;
			GetCellBounds(FIntPoint(X, Y), BoxMinX, BoxMinY, IgnoredMaxX, IgnoredMaxY);
			double IgnoredMinX;
			double IgnoredMinY;
			double BoxMaxX;
			double BoxMaxY;
			GetCellBounds(
				FIntPoint(X + MergeWidth - 1, Y + MergeHeight - 1),
				IgnoredMinX, IgnoredMinY, BoxMaxX, BoxMaxY);
			FKBoxElem& Box = OutCollision.BoxElems.Emplace_GetRef(
				static_cast<float>(BoxMaxX - BoxMinX),
				static_cast<float>(BoxMaxY - BoxMinY),
				static_cast<float>(EffectiveCellDimensions.Z));
			Box.Center = FVector(
				(BoxMinX + BoxMaxX) * 0.5,
				(BoxMinY + BoxMaxY) * 0.5,
				-EffectiveCellDimensions.Z * 0.5);
		}
	}

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (!Cells[ToCellIndex(Cell)].bExists || IsCellFullyInsideGeneratedShape(Cell))
			{
				continue;
			}

			TArray<TArray<FVector2D>> Fragments;
			if (!GetClippedCellFragments(Cell, Fragments))
			{
				continue;
			}
			for (const TArray<FVector2D>& Fragment : Fragments)
			{
				FKConvexElem& Convex = OutCollision.ConvexElems.Emplace_GetRef();
				Convex.VertexData.Reserve(Fragment.Num() * 2);
				for (const FVector2D& Point : Fragment)
				{
					Convex.VertexData.Add(FVector(Point.X, Point.Y, 0.0));
					Convex.VertexData.Add(FVector(Point.X, Point.Y, -EffectiveCellDimensions.Z));
				}
				Convex.UpdateElemBox();
			}
		}
	}
}

FTransform AChunkedDestructibleDecorativeFloor::GetCellFlashTransform(FIntPoint Cell) const
{
	if (IsCellFullyInsideGeneratedShape(Cell))
	{
		return Super::GetCellFlashTransform(Cell);
	}

	TArray<TArray<FVector2D>> Fragments;
	if (!GetClippedCellFragments(Cell, Fragments))
	{
		return Super::GetCellFlashTransform(Cell);
	}

	FBox2D Bounds(EForceInit::ForceInit);
	for (const TArray<FVector2D>& Fragment : Fragments)
	{
		for (const FVector2D& Point : Fragment)
		{
			Bounds += Point;
		}
	}
	const FVector2D Size = Bounds.GetSize();
	const FVector2D Center = Bounds.GetCenter();
	const float SafeFlashScale = FMath::Max(FlashScale, 0.01f);
	return FTransform(
		FQuat::Identity,
		FVector(Center.X, Center.Y, -EffectiveCellDimensions.Z * 0.5),
		FVector(Size.X / 100.0, Size.Y / 100.0, EffectiveCellDimensions.Z / 100.0) * SafeFlashScale);
}

void AChunkedDestructibleDecorativeFloor::AppendHeartSurface(
	UE::Geometry::FDynamicMesh3& Mesh,
	int32 ChunkIndex,
	float Scale,
	int32 MaterialIndex,
	double SurfaceZ) const
{
	const TArray<FVector2D> Outline = BuildParametricHeartOutline(
		HeartSize,
		HeartOffset,
		HeartRotationDegrees,
		Scale,
		HeartCurveResolution);
	AppendPolygonSurface(Mesh, ChunkIndex, Outline, MaterialIndex, SurfaceZ);
}

void AChunkedDestructibleDecorativeFloor::AppendStarSurface(
	UE::Geometry::FDynamicMesh3& Mesh,
	int32 ChunkIndex,
	float Scale,
	int32 MaterialIndex,
	double SurfaceZ) const
{
	const TArray<FVector2D> Outline = BuildStarOutline(
		MotifStarSize,
		MotifStarOffset,
		MotifStarRotationDegrees,
		Scale,
		MotifStarPointCount,
		MotifStarInnerRadius);
	AppendPolygonSurface(Mesh, ChunkIndex, Outline, MaterialIndex, SurfaceZ);
}

void AChunkedDestructibleDecorativeFloor::AppendFlowerPattern(
	UE::Geometry::FDynamicMesh3& Mesh,
	int32 ChunkIndex) const
{
	if (ChunkCount.X <= 0 || ChunkIndex < 0 || ChunkIndex >= ChunkCount.X * ChunkCount.Y)
	{
		return;
	}

	const FVector2D BaseFlowerHalfSize(
		FMath::Max(FMath::Abs(FlowerMotifSettings.FlowerSize.X), 50.0) * 0.5,
		FMath::Max(FMath::Abs(FlowerMotifSettings.FlowerSize.Y), 50.0) * 0.5);
	const bool bSimpleFlower = FlowerMotifSettings.FlowerType != EChunkedFlowerType::Daisy;
	const float TypeScale = bSimpleFlower
		? FMath::Clamp(FlowerMotifSettings.LargeTypeScale, 1.0f, 4.0f)
		: 1.0f;
	const FVector2D FlowerCenter = FlowerMotifSettings.Offset;
	const FVector2D FlowerHalfSize = BaseFlowerHalfSize * TypeScale;
	const float FlowerRotation = FMath::DegreesToRadians(FlowerMotifSettings.RotationDegrees);
	const int32 PetalCount = bSimpleFlower
		? FMath::Clamp(FlowerMotifSettings.SimplePetalCount, 3, 12)
		: FMath::Clamp(FlowerMotifSettings.PetalCount, 3, 24);
	const float PetalLength = bSimpleFlower
		? FMath::Clamp(FlowerMotifSettings.SimplePetalLengthRatio, 0.3f, 0.8f)
		: FMath::Clamp(FlowerMotifSettings.PetalLengthRatio, 0.15f, 0.75f);
	const float PetalWidth = bSimpleFlower
		? FMath::Clamp(FlowerMotifSettings.SimplePetalWidthRatio, 0.2f, 0.8f)
		: FMath::Clamp(FlowerMotifSettings.PetalWidthRatio, 0.08f, 0.65f);
	const float CenterRadius = bSimpleFlower
		? FMath::Clamp(FlowerMotifSettings.SimpleCenterRadiusRatio, 0.15f, 0.65f)
		: FMath::Clamp(FlowerMotifSettings.CenterRadiusRatio, 0.05f, 0.55f);
	const int32 Resolution = FMath::Clamp(FlowerMotifSettings.CurveResolution, 8, 64);
	const int32 OutlineMaterial = ClampMaterialIndex(FlowerMotifSettings.OutlineMaterialIndex);
	const int32 CenterMaterial = ClampMaterialIndex(FlowerMotifSettings.CenterMaterialIndex);
	const float NormalizedOutline = FlowerMotifSettings.OutlineWidth /
		FMath::Max(FMath::Min(FlowerHalfSize.X, FlowerHalfSize.Y), 1.0);
	const FVector2D InnerPetalHalfSize(
		FMath::Max(PetalLength - NormalizedOutline, 0.01f),
		FMath::Max(PetalWidth - NormalizedOutline, 0.01f));
	const float InnerCenterRadius = FMath::Max(CenterRadius - NormalizedOutline, 0.01f);

	for (int32 PetalIndex = 0; PetalIndex < PetalCount; ++PetalIndex)
	{
		const float PetalAngle = UE_TWO_PI * static_cast<float>(PetalIndex) / PetalCount;
		const FVector2D PetalDirection(FMath::Cos(PetalAngle), FMath::Sin(PetalAngle));
		const FVector2D PetalCenter = PetalDirection * (1.0f - PetalLength);
		AppendPolygonSurface(
			Mesh,
			ChunkIndex,
			BuildFlowerEllipseOutline(
				FlowerCenter,
				FlowerHalfSize,
				PetalCenter,
				FVector2D(PetalLength, PetalWidth),
				PetalAngle,
				FlowerRotation,
				Resolution),
			OutlineMaterial,
			HeartSurfaceLift);
		AppendPolygonSurface(
			Mesh,
			ChunkIndex,
			BuildFlowerEllipseOutline(
				FlowerCenter,
				FlowerHalfSize,
				PetalCenter,
				InnerPetalHalfSize,
				PetalAngle,
				FlowerRotation,
				Resolution),
			CenterMaterial,
			HeartSurfaceLift + HeartLayerSeparation);
	}

	if (FlowerMotifSettings.FlowerType == EChunkedFlowerType::LargeHeart)
	{
		const FVector2D HeartCenterSize = FlowerHalfSize * (CenterRadius * 2.0f);
		const int32 HeartResolution = FMath::Clamp(Resolution * 2, 32, 128);
		AppendPolygonSurface(
			Mesh,
			ChunkIndex,
			BuildParametricHeartOutline(
				HeartCenterSize,
				FlowerCenter,
				FMath::RadiansToDegrees(FlowerRotation),
				1.0f,
				HeartResolution),
			OutlineMaterial,
			HeartSurfaceLift + HeartLayerSeparation * 2.0);
		AppendPolygonSurface(
			Mesh,
			ChunkIndex,
			BuildParametricHeartOutline(
				HeartCenterSize,
				FlowerCenter,
				FMath::RadiansToDegrees(FlowerRotation),
				InnerCenterRadius / CenterRadius,
				HeartResolution),
			CenterMaterial,
			HeartSurfaceLift + HeartLayerSeparation * 3.0);
	}
	else
	{
		AppendPolygonSurface(
			Mesh,
			ChunkIndex,
			BuildFlowerEllipseOutline(
				FlowerCenter,
				FlowerHalfSize,
				FVector2D::ZeroVector,
				FVector2D(CenterRadius, CenterRadius),
				0.0f,
				FlowerRotation,
				Resolution),
			OutlineMaterial,
			HeartSurfaceLift + HeartLayerSeparation * 2.0);
		AppendPolygonSurface(
			Mesh,
			ChunkIndex,
			BuildFlowerEllipseOutline(
				FlowerCenter,
				FlowerHalfSize,
				FVector2D::ZeroVector,
				FVector2D(InnerCenterRadius, InnerCenterRadius),
				0.0f,
				FlowerRotation,
				Resolution),
			CenterMaterial,
			HeartSurfaceLift + HeartLayerSeparation * 3.0);
	}
}

void AChunkedDestructibleDecorativeFloor::AppendPolygonSurface(
	UE::Geometry::FDynamicMesh3& Mesh,
	int32 ChunkIndex,
	const TArray<FVector2D>& Outline,
	int32 MaterialIndex,
	double SurfaceZ) const
{
	if (Outline.Num() < 3 || ChunkCount.X <= 0)
	{
		return;
	}
	FPatternSurfaceCache& SurfaceCache = FindOrAddPatternSurfaceCache(
		Outline, MaterialIndex, SurfaceZ);
	if (SurfaceCache.SourceTriangles.IsEmpty())
	{
		return;
	}

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	const int32 ChunkX = ChunkIndex % ChunkCount.X;
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinCellX = ChunkX * SafeChunkSide;
	const int32 MinCellY = ChunkY * SafeChunkSide;
	const int32 MaxCellX = FMath::Min(MinCellX + SafeChunkSide, CellCount.X);
	const int32 MaxCellY = FMath::Min(MinCellY + SafeChunkSide, CellCount.Y);
	const double HalfCellX = EffectiveCellDimensions.X * 0.5;
	const double HalfCellY = EffectiveCellDimensions.Y * 0.5;
	const float SafeUVWorldUnits = FMath::Max(UVWorldUnits, 1.0f);

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const FIntPoint Cell(X, Y);
			const int32 CellIndex = ToCellIndex(Cell);
			if (!Cells[CellIndex].bExists || !SurfaceCache.CellTriangles.IsValidIndex(CellIndex) ||
				!SurfaceCache.CellBuilt.IsValidIndex(CellIndex))
			{
				continue;
			}
			TArray<FCachedPatternTriangle>& CachedTriangles = SurfaceCache.CellTriangles[CellIndex];
			if (SurfaceCache.CellBuilt[CellIndex])
			{
				for (const FCachedPatternTriangle& Triangle : CachedTriangles)
				{
					AppendMeshTriangle(
						Mesh,
						FVector3d(Triangle.A.X, Triangle.A.Y, SurfaceZ),
						FVector3d(Triangle.B.X, Triangle.B.Y, SurfaceZ),
						FVector3d(Triangle.C.X, Triangle.C.Y, SurfaceZ),
						FVector3f::UnitZ(), MaterialIndex,
						FVector2f(Triangle.A.X / SafeUVWorldUnits, Triangle.A.Y / SafeUVWorldUnits),
						FVector2f(Triangle.B.X / SafeUVWorldUnits, Triangle.B.Y / SafeUVWorldUnits),
						FVector2f(Triangle.C.X / SafeUVWorldUnits, Triangle.C.Y / SafeUVWorldUnits));
				}
				continue;
			}
			CachedTriangles.Reset();

			const FVector Center3D = GetCellLocalCenter(Cell);
			const FVector2D RectangleMin(Center3D.X - HalfCellX, Center3D.Y - HalfCellY);
			const FVector2D RectangleMax(Center3D.X + HalfCellX, Center3D.Y + HalfCellY);
			if (RectangleMax.X < SurfaceCache.BoundsMin.X || RectangleMin.X > SurfaceCache.BoundsMax.X ||
				RectangleMax.Y < SurfaceCache.BoundsMin.Y || RectangleMin.Y > SurfaceCache.BoundsMax.Y)
			{
				SurfaceCache.CellBuilt[CellIndex] = true;
				continue;
			}

			TArray<TArray<FVector2D>> ShapeFragments;
			if (IsCellFullyInsideGeneratedShape(Cell))
			{
				ShapeFragments.Add({
					RectangleMin,
					FVector2D(RectangleMax.X, RectangleMin.Y),
					RectangleMax,
					FVector2D(RectangleMin.X, RectangleMax.Y)
				});
			}
			else if (!GetClippedCellFragments(Cell, ShapeFragments))
			{
				continue;
			}

			for (const FPatternSourceTriangle& Triangle : SurfaceCache.SourceTriangles)
			{
				if (Triangle.Max.X < RectangleMin.X || Triangle.Min.X > RectangleMax.X ||
					Triangle.Max.Y < RectangleMin.Y || Triangle.Min.Y > RectangleMax.Y)
				{
					continue;
				}

				FHeartTriangle SourceTriangle;
				SourceTriangle.A = Triangle.A;
				SourceTriangle.B = Triangle.B;
				SourceTriangle.C = Triangle.C;
				const TArray<FVector2D> CellClipped = ClipTriangleToRectangle(
					SourceTriangle,
					RectangleMin,
					RectangleMax);
				for (const TArray<FVector2D>& ShapeFragment : ShapeFragments)
				{
					const TArray<FVector2D> Clipped = ClipPolygonToConvexPolygon(
						CellClipped,
						ShapeFragment);
					for (int32 VertexIndex = 1; VertexIndex + 1 < Clipped.Num(); ++VertexIndex)
					{
						const FVector2D& A = Clipped[0];
						const FVector2D& B = Clipped[VertexIndex];
						const FVector2D& C = Clipped[VertexIndex + 1];
						if (FMath::Abs(Cross2D(B - A, C - A)) <= HeartPolygonTolerance)
						{
							continue;
						}

						FCachedPatternTriangle& CachedTriangle = CachedTriangles.Emplace_GetRef();
						CachedTriangle.A = A;
						CachedTriangle.B = B;
						CachedTriangle.C = C;
					}
				}
			}
			SurfaceCache.CellBuilt[CellIndex] = true;
			for (const FCachedPatternTriangle& Triangle : CachedTriangles)
			{
				AppendMeshTriangle(
					Mesh,
					FVector3d(Triangle.A.X, Triangle.A.Y, SurfaceZ),
					FVector3d(Triangle.B.X, Triangle.B.Y, SurfaceZ),
					FVector3d(Triangle.C.X, Triangle.C.Y, SurfaceZ),
					FVector3f::UnitZ(), MaterialIndex,
					FVector2f(Triangle.A.X / SafeUVWorldUnits, Triangle.A.Y / SafeUVWorldUnits),
					FVector2f(Triangle.B.X / SafeUVWorldUnits, Triangle.B.Y / SafeUVWorldUnits),
					FVector2f(Triangle.C.X / SafeUVWorldUnits, Triangle.C.Y / SafeUVWorldUnits));
			}
		}
	}
}

AChunkedDestructibleDecorativeFloor::FPatternSurfaceCache&
AChunkedDestructibleDecorativeFloor::FindOrAddPatternSurfaceCache(
	const TArray<FVector2D>& Outline,
	int32 MaterialIndex,
	double SurfaceZ) const
{
	auto OutlinesMatch = [&Outline](const TArray<FVector2D>& Candidate)
	{
		if (Candidate.Num() != Outline.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Outline.Num(); ++Index)
		{
			if (Candidate[Index] != Outline[Index])
			{
				return false;
			}
		}
		return true;
	};
	for (FPatternSurfaceCache& Cache : PatternSurfaceCaches)
	{
		if (Cache.MaterialIndex == MaterialIndex && Cache.SurfaceZ == SurfaceZ &&
			OutlinesMatch(Cache.Outline))
		{
			return Cache;
		}
	}

	// Repeated live editing can leave inactive motif variants behind. Bound that editor-only
	// accumulation; active entries are rebuilt lazily with exactly the same geometry.
	if (PatternSurfaceCaches.Num() >= 256)
	{
		PatternSurfaceCaches.Reset();
	}
	FPatternSurfaceCache& Cache = PatternSurfaceCaches.Emplace_GetRef();
	Cache.Outline = Outline;
	Cache.MaterialIndex = MaterialIndex;
	Cache.SurfaceZ = SurfaceZ;
	Cache.BoundsMin = FVector2D(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	Cache.BoundsMax = FVector2D(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
	for (const FVector2D& Point : Outline)
	{
		Cache.BoundsMin.X = FMath::Min(Cache.BoundsMin.X, Point.X);
		Cache.BoundsMin.Y = FMath::Min(Cache.BoundsMin.Y, Point.Y);
		Cache.BoundsMax.X = FMath::Max(Cache.BoundsMax.X, Point.X);
		Cache.BoundsMax.Y = FMath::Max(Cache.BoundsMax.Y, Point.Y);
	}
	const TArray<FHeartTriangle> Triangles = TriangulateHeartOutline(Outline);
	Cache.SourceTriangles.Reserve(Triangles.Num());
	for (const FHeartTriangle& Triangle : Triangles)
	{
		FPatternSourceTriangle& CachedTriangle = Cache.SourceTriangles.Emplace_GetRef();
		CachedTriangle.A = Triangle.A;
		CachedTriangle.B = Triangle.B;
		CachedTriangle.C = Triangle.C;
		CachedTriangle.Min = Triangle.Min;
		CachedTriangle.Max = Triangle.Max;
	}
	Cache.CellTriangles.SetNum(Cells.Num());
	Cache.CellBuilt.Init(false, Cells.Num());
	return Cache;
}
