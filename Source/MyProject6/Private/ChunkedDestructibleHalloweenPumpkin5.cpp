// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleHalloweenPumpkin5.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DestructibleBoxCollision.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"

using namespace UE::Geometry;

namespace
{
	constexpr double Pumpkin5FieldEpsilon = 1.e-8;

	double SignedPolygonDistance5(const FVector2D& Point, const TArray<FVector2D>& Polygon)
	{
		if (Polygon.Num() < 3)
		{
			return -1.0;
		}

		double MinimumDistanceSquared = TNumericLimits<double>::Max();
		bool bInside = false;
		for (int32 Index = 0, PreviousIndex = Polygon.Num() - 1; Index < Polygon.Num(); PreviousIndex = Index++)
		{
			const FVector2D& A = Polygon[PreviousIndex];
			const FVector2D& B = Polygon[Index];
			const FVector2D Edge = B - A;
			const double EdgeLengthSquared = Edge.SquaredLength();
			const double Alpha = EdgeLengthSquared > UE_DOUBLE_SMALL_NUMBER
				? FMath::Clamp(FVector2D::DotProduct(Point - A, Edge) / EdgeLengthSquared, 0.0, 1.0)
				: 0.0;
			MinimumDistanceSquared = FMath::Min(
				MinimumDistanceSquared,
				(Point - (A + Edge * Alpha)).SquaredLength());

			const bool bStraddles = (A.Y > Point.Y) != (B.Y > Point.Y);
			if (bStraddles)
			{
				const double IntersectionX = A.X +
					(Point.Y - A.Y) * (B.X - A.X) / FMath::Max(FMath::Abs(B.Y - A.Y), UE_DOUBLE_SMALL_NUMBER) *
					(B.Y >= A.Y ? 1.0 : -1.0);
				if (Point.X < IntersectionX)
				{
					bInside = !bInside;
				}
			}
		}
		const double Distance = FMath::Sqrt(FMath::Max(MinimumDistanceSquared, 0.0));
		return bInside ? Distance : -Distance;
	}

	double RectangleField5(const FVector2D& Point, const FVector2D& Center, const FVector2D& HalfSize)
	{
		return FMath::Min(
			HalfSize.X - FMath::Abs(Point.X - Center.X),
			HalfSize.Y - FMath::Abs(Point.Y - Center.Y));
	}

	FVector InterpolateIsoPoint5(
		const FVector& A,
		const FVector& B,
		double FieldA,
		double FieldB)
	{
		const double Denominator = FieldA - FieldB;
		const double Alpha = FMath::Abs(Denominator) > Pumpkin5FieldEpsilon
			? FMath::Clamp(FieldA / Denominator, 0.0, 1.0)
			: 0.5;
		return FMath::Lerp(A, B, Alpha);
	}
}

AChunkedDestructibleHalloweenPumpkin5::AChunkedDestructibleHalloweenPumpkin5()
{
	FloorPattern = EChunkedFloorPattern::Solid;
	// P5 interprets this as the side of a true spatial 3D chunk. Eight keeps a bullet hit local.
	ChunkSideLength = 8;
	bGenerateBottomFaces = true;
	bDestructibleByDefault = true;
	DefaultCellHealth = 25.0f;
	FlashColor = FLinearColor(1.0f, 0.16f, 0.005f, 1.0f);

	// Squat, full proportions and a short upright stem match the supplied photograph.
	BodyRadiusBlocks = FVector2D(9.6f, 11.4f);
	BodyHalfHeightBlocks = 8.25f;
	VerticalProfilePower = 2.45f;
	RibCount = 10;
	RibDepth = 0.095f;
	ShellThicknessBlocks = 2.25f;
	BlockDimensions = FVector(27.0f, 27.0f, 27.0f);
	BlockSpacing = FVector::ZeroVector;

	bCarveJackOLanternFace = true;
	// The openings are real holes. In particular, do not place the inherited emissive card behind them.
	bEnableFaceGlowSurface = false;
	FaceGlowIntensity = 0.0f;

	StemRadiusBlocks = 1.25f;
	StemHeightBlocks = 3.8f;
	StemLeanBlocks = FVector2D(-0.15f, -0.45f);
	StemCurveBlocks = FVector2D(0.10f, -0.20f);

	bEnableInteriorLight = true;
	InteriorLightColor = FLinearColor(1.0f, 0.22f, 0.006f, 1.0f);
	InteriorLightIntensity = 650.0f;
	InteriorLightAttenuationRadius = 900.0f;
	bInteriorLightCastsShadows = true;

	BodyColor = FLinearColor(0.72f, 0.095f, 0.008f, 1.0f);
	RibColor = FLinearColor(0.29f, 0.028f, 0.002f, 1.0f);
	InteriorColor = FLinearColor(1.0f, 0.25f, 0.008f, 1.0f);
	StemColor = FLinearColor(0.085f, 0.055f, 0.018f, 1.0f);

	SoftEdgeBlend = 1.0f;
	EdgeRounding = 1.0f;
	SurfaceSubdivisions = 2;
	bCapDestroyedCellEdges = true;
	bUseAsyncCollisionCooking = true;
	SpatialChunkSideLength = 8;
}

void AChunkedDestructibleHalloweenPumpkin5::ConfigureReferencePumpkin(
	FVector2D NewBodyRadiusBlocks,
	float NewBodyHalfHeightBlocks,
	float NewShellThicknessBlocks,
	float NewStemHeightBlocks,
	FVector NewBlockDimensions,
	int32 NewSurfaceSubdivisions,
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
	BlockSpacing = FVector::ZeroVector;
	SurfaceSubdivisions = FMath::Clamp(NewSurfaceSubdivisions, 1, 4);
	RebuildPumpkin2(bPreserveCellState);
}

void AChunkedDestructibleHalloweenPumpkin5::SetReferenceFaceEnabled(
	bool bEnabled,
	bool bPreserveCellState)
{
	bCarveJackOLanternFace = bEnabled;
	bEnableFaceGlowSurface = false;
	RebuildPumpkin2(bPreserveCellState);
}

bool AChunkedDestructibleHalloweenPumpkin5::CalculateLayout()
{
	if (!Super::CalculateLayout())
	{
		SpatialChunkCount = FIntVector::ZeroValue;
		CachedCellSurfaceTriangles.Reset();
		CachedCellSurfaceBuilt.Reset();
		return false;
	}

	// The inherited pumpkin flattens X/Y into one logical coordinate. Repartition that backing
	// grid into actual spatial cubes so one bullet no longer rebuilds a long slice of the pumpkin.
	const int32 Side = GetEffectiveSpatialChunkSideLength();
	SpatialChunkCount = FIntVector(
		FMath::DivideAndRoundUp(PumpkinGridCellCount.X, Side),
		FMath::DivideAndRoundUp(PumpkinGridCellCount.Y, Side),
		FMath::DivideAndRoundUp(PumpkinGridCellCount.Z, Side));
	ChunkCount = FIntPoint(
		SpatialChunkCount.X * SpatialChunkCount.Y,
		SpatialChunkCount.Z);

	const int32 TotalCells = CellCount.X * CellCount.Y;
	CachedCellSurfaceTriangles.SetNum(TotalCells);
	CachedCellSurfaceBuilt.Init(false, TotalCells);
	return true;
}

int32 AChunkedDestructibleHalloweenPumpkin5::GetEffectiveSpatialChunkSideLength() const
{
	return FMath::Clamp(SpatialChunkSideLength, 4, 12);
}

double AChunkedDestructibleHalloweenPumpkin5::EvaluateBodyField(
	const FVector& LocalPosition,
	float InsetBlocks) const
{
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const double SafeInset = FMath::Max(InsetBlocks, 0.0f);
	const double RadiusX = FMath::Max((BodyRadiusBlocks.X - SafeInset) * Pitch.X, 1.0);
	const double RadiusY = FMath::Max((BodyRadiusBlocks.Y - SafeInset) * Pitch.Y, 1.0);
	const double HalfHeight = FMath::Max((BodyHalfHeightBlocks - SafeInset) * Pitch.Z, 1.0);
	if (BodyRadiusBlocks.X - SafeInset <= 0.5 ||
		BodyRadiusBlocks.Y - SafeInset <= 0.5 ||
		BodyHalfHeightBlocks - SafeInset <= 0.5)
	{
		return -1.0;
	}

	const double NormalizedX = LocalPosition.X / RadiusX;
	const double NormalizedY = LocalPosition.Y / RadiusY;
	const double NormalizedZ = (LocalPosition.Z - GetBodyCenterZ()) / HalfHeight;
	const double Angle = FMath::Atan2(NormalizedY, NormalizedX);
	const double GrooveWave = 0.5 * (1.0 + FMath::Cos(FMath::Max(RibCount, 1) * Angle));
	const double InsetFade = InsetBlocks > 0.0f ? 0.42 : 1.0;
	const double LobeRadius = FMath::Max(1.0 - RibDepth * InsetFade * GrooveWave, 0.55);
	const double Radial = FMath::Sqrt(NormalizedX * NormalizedX + NormalizedY * NormalizedY) /
		LobeRadius;
	const double Power = FMath::Clamp(static_cast<double>(VerticalProfilePower), 1.5, 6.0);
	return 1.0 - FMath::Pow(Radial, Power) - FMath::Pow(FMath::Abs(NormalizedZ), Power);
}

double AChunkedDestructibleHalloweenPumpkin5::EvaluateStemField(const FVector& LocalPosition) const
{
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const double Bottom = GetStemBottomZ() - Pitch.Z * 0.35;
	const double Height = FMath::Max(StemHeightBlocks * Pitch.Z, 1.0);
	const double Alpha = (LocalPosition.Z - Bottom) / Height;
	const double ClampedAlpha = FMath::Clamp(Alpha, 0.0, 1.0);
	const double SmoothAlpha = ClampedAlpha * ClampedAlpha * (3.0 - 2.0 * ClampedAlpha);
	const double CurveAlpha = FMath::Sin(ClampedAlpha * UE_PI);
	const FVector2D StemCenter(
		(StemLeanBlocks.X * SmoothAlpha + StemCurveBlocks.X * CurveAlpha) * Pitch.X,
		(StemLeanBlocks.Y * SmoothAlpha + StemCurveBlocks.Y * CurveAlpha) * Pitch.Y);
	const double Taper = FMath::Lerp(1.0, 0.64, ClampedAlpha);
	const double RadiusX = FMath::Max(StemRadiusBlocks * Pitch.X * Taper, 1.0);
	const double RadiusY = FMath::Max(StemRadiusBlocks * Pitch.Y * Taper * 0.88, 1.0);
	const double DeltaX = (LocalPosition.X - StemCenter.X) / RadiusX;
	const double DeltaY = (LocalPosition.Y - StemCenter.Y) / RadiusY;
	const double RadialField = 1.0 - FMath::Sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
	const double BottomField = (LocalPosition.Z - Bottom) / FMath::Max(Pitch.Z * 0.35, 1.0);
	const double TopField = (Bottom + Height - LocalPosition.Z) / FMath::Max(Pitch.Z * 0.35, 1.0);
	return FMath::Min3(RadialField, BottomField, TopField);
}

double AChunkedDestructibleHalloweenPumpkin5::EvaluateFaceProjectionField(
	const FVector& LocalPosition) const
{
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const double Horizontal = LocalPosition.Y /
		FMath::Max(BodyRadiusBlocks.Y * Pitch.Y, 1.0);
	const double Vertical = (LocalPosition.Z - GetBodyCenterZ()) /
		FMath::Max(BodyHalfHeightBlocks * Pitch.Z, 1.0);
	const FVector2D Point(Horizontal, Vertical);

	// Angry, inward-drooping quadrilateral eyes from the reference photograph.
	const TArray<FVector2D> LeftEye = {
		FVector2D(-0.76, 0.43), FVector2D(-0.20, 0.27),
		FVector2D(-0.15, 0.06), FVector2D(-0.66, 0.09)
	};
	const TArray<FVector2D> RightEye = {
		FVector2D(0.20, 0.27), FVector2D(0.76, 0.43),
		FVector2D(0.66, 0.09), FVector2D(0.15, 0.06)
	};
	const TArray<FVector2D> Nose = {
		FVector2D(0.0, 0.08), FVector2D(-0.15, -0.18), FVector2D(0.15, -0.18)
	};
	double FaceField = FMath::Max3(
		SignedPolygonDistance5(Point, LeftEye),
		SignedPolygonDistance5(Point, RightEye),
		SignedPolygonDistance5(Point, Nose));

	// Broad rising grin. Two upper and three lower solid teeth interrupt the opening.
	const double AbsoluteHorizontal = FMath::Abs(Horizontal);
	const double Across = AbsoluteHorizontal / 0.88;
	const double MouthCenter = -0.43 + 0.30 * FMath::Pow(FMath::Min(Across, 1.0), 1.55);
	const double MouthHalfThickness = 0.135 + 0.018 * (1.0 - FMath::Min(Across, 1.0));
	double MouthField = FMath::Min(0.88 - AbsoluteHorizontal,
		MouthHalfThickness - FMath::Abs(Vertical - MouthCenter));
	const double UpperToothField = FMath::Max(
		RectangleField5(Point, FVector2D(-0.29, -0.37), FVector2D(0.075, 0.12)),
		RectangleField5(Point, FVector2D(0.29, -0.37), FVector2D(0.075, 0.12)));
	const double LowerToothField = FMath::Max3(
		RectangleField5(Point, FVector2D(-0.50, -0.49), FVector2D(0.075, 0.11)),
		RectangleField5(Point, FVector2D(0.0, -0.53), FVector2D(0.08, 0.12)),
		RectangleField5(Point, FVector2D(0.50, -0.49), FVector2D(0.075, 0.11)));
	MouthField = FMath::Min(MouthField, -FMath::Max(UpperToothField, LowerToothField));
	return FMath::Max(FaceField, MouthField);
}

bool AChunkedDestructibleHalloweenPumpkin5::IsFaceProjection(const FVector& LocalCenter) const
{
	return EvaluateFaceProjectionField(LocalCenter) >= 0.0;
}

bool AChunkedDestructibleHalloweenPumpkin5::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return false;
	}

	// Boundary cells must be retained even when their centre lies just outside the analytic shell.
	// Sampling the complete cell volume lets marching tetrahedra reach both the outer skin and the
	// cavity skin, while cells wholly inside the cavity/face openings remain physically absent.
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	for (int32 SampleZ = -1; SampleZ <= 1; ++SampleZ)
	{
		for (int32 SampleY = -1; SampleY <= 1; ++SampleY)
		{
			for (int32 SampleX = -1; SampleX <= 1; ++SampleX)
			{
				const FVector Position = Center + FVector(
					Pitch.X * 0.5 * SampleX,
					Pitch.Y * 0.5 * SampleY,
					Pitch.Z * 0.5 * SampleZ);
				if (EvaluateSolidField(Position) >= 0.0)
				{
					return true;
				}
			}
		}
	}
	return false;
}

double AChunkedDestructibleHalloweenPumpkin5::EvaluateSolidField(const FVector& LocalPosition) const
{
	const double OuterField = EvaluateBodyField(LocalPosition, 0.0f);
	const double InnerField = EvaluateBodyField(LocalPosition, ShellThicknessBlocks);
	double BodyShellField = FMath::Min(OuterField, -InnerField);
	if (bCarveJackOLanternFace)
	{
		const FVector Pitch = BlockDimensions + BlockSpacing;
		const double FrontHalfField = LocalPosition.X / FMath::Max(Pitch.X * 0.45, 1.0);
		const double FaceCutField = FMath::Min(EvaluateFaceProjectionField(LocalPosition), FrontHalfField);
		BodyShellField = FMath::Min(BodyShellField, -FaceCutField);
	}
	return FMath::Max(BodyShellField, EvaluateStemField(LocalPosition));
}

FVector3f AChunkedDestructibleHalloweenPumpkin5::EvaluateSurfaceNormal(
	const FVector& LocalPosition) const
{
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const double Epsilon = FMath::Max(Pitch.GetMin() * 0.035, 0.25);
	const double DX = EvaluateSolidField(LocalPosition + FVector(Epsilon, 0.0, 0.0)) -
		EvaluateSolidField(LocalPosition - FVector(Epsilon, 0.0, 0.0));
	const double DY = EvaluateSolidField(LocalPosition + FVector(0.0, Epsilon, 0.0)) -
		EvaluateSolidField(LocalPosition - FVector(0.0, Epsilon, 0.0));
	const double DZ = EvaluateSolidField(LocalPosition + FVector(0.0, 0.0, Epsilon)) -
		EvaluateSolidField(LocalPosition - FVector(0.0, 0.0, Epsilon));
	return FVector3f(static_cast<float>(-DX), static_cast<float>(-DY), static_cast<float>(-DZ)).GetSafeNormal();
}

int32 AChunkedDestructibleHalloweenPumpkin5::EvaluateSurfaceMaterial(
	const FVector& LocalPosition) const
{
	const double BodyShell = FMath::Min(
		EvaluateBodyField(LocalPosition, 0.0f),
		-EvaluateBodyField(LocalPosition, ShellThicknessBlocks));
	const double Stem = EvaluateStemField(LocalPosition);
	if (Stem >= BodyShell && FMath::Abs(Stem) < 0.12)
	{
		return StemMaterialSlot;
	}

	const double OuterDistance = FMath::Abs(EvaluateBodyField(LocalPosition, 0.0f));
	const double InnerDistance = FMath::Abs(EvaluateBodyField(LocalPosition, ShellThicknessBlocks));
	if (InnerDistance < OuterDistance)
	{
		return InteriorMaterialSlot;
	}
	if (bCarveJackOLanternFace && EvaluateFaceProjectionField(LocalPosition) > -0.035 && LocalPosition.X > 0.0f)
	{
		return InteriorMaterialSlot;
	}

	const FVector Pitch = BlockDimensions + BlockSpacing;
	const double NormalizedX = LocalPosition.X / FMath::Max(BodyRadiusBlocks.X * Pitch.X, 1.0);
	const double NormalizedY = LocalPosition.Y / FMath::Max(BodyRadiusBlocks.Y * Pitch.Y, 1.0);
	const double Groove = FMath::Cos(FMath::Max(RibCount, 1) * FMath::Atan2(NormalizedY, NormalizedX));
	return Groove > 0.64 ? RibMaterialSlot : BodyMaterialSlot;
}

void AChunkedDestructibleHalloweenPumpkin5::AppendSmoothTriangle(
	FDynamicMesh3& Mesh,
	FVector A,
	FVector B,
	FVector C,
	FVector3f NormalA,
	FVector3f NormalB,
	FVector3f NormalC,
	int32 MaterialIndex) const
{
	FVector FaceNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
	const FVector ExpectedNormal = FVector(NormalA + NormalB + NormalC).GetSafeNormal();
	if (FVector::DotProduct(FaceNormal, ExpectedNormal) < 0.0)
	{
		Swap(B, C);
		Swap(NormalB, NormalC);
	}

	const int32 VertexA = Mesh.AppendVertex(FVector3d(A));
	const int32 VertexB = Mesh.AppendVertex(FVector3d(B));
	const int32 VertexC = Mesh.AppendVertex(FVector3d(C));
	Mesh.SetVertexNormal(VertexA, NormalA);
	Mesh.SetVertexNormal(VertexB, NormalB);
	Mesh.SetVertexNormal(VertexC, NormalC);
	const int32 Triangle = Mesh.AppendTriangle(VertexA, VertexC, VertexB);
	if (Triangle < 0 || !Mesh.HasAttributes())
	{
		return;
	}

	if (FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals())
	{
		const int32 AIndex = Normals->AppendElement(NormalA);
		const int32 BIndex = Normals->AppendElement(NormalB);
		const int32 CIndex = Normals->AppendElement(NormalC);
		Normals->SetTriangle(Triangle, FIndex3i(AIndex, CIndex, BIndex));
	}
	if (FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV())
	{
		const float Scale = FMath::Max(UVWorldUnits, 1.0f);
		auto MakeUV = [Scale](const FVector& Point)
		{
			return FVector2f(static_cast<float>(Point.Y / Scale), static_cast<float>(Point.Z / Scale));
		};
		const int32 AIndex = UVs->AppendElement(MakeUV(A));
		const int32 BIndex = UVs->AppendElement(MakeUV(B));
		const int32 CIndex = UVs->AppendElement(MakeUV(C));
		UVs->SetTriangle(Triangle, FIndex3i(AIndex, CIndex, BIndex));
	}
	if (FDynamicMeshMaterialAttribute* Materials = Mesh.Attributes()->GetMaterialID())
	{
		Materials->SetValue(Triangle, FMath::Max(MaterialIndex, 0));
	}
}

void AChunkedDestructibleHalloweenPumpkin5::AppendMarchingTetrahedron(
	TArray<FCachedSurfaceTriangle>& OutTriangles,
	const FVector* Positions,
	const double* Fields,
	const int32* Tetrahedron) const
{
	int32 Inside[4];
	int32 Outside[4];
	int32 InsideCount = 0;
	int32 OutsideCount = 0;
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const int32 Index = Tetrahedron[Corner];
		if (Fields[Index] >= 0.0)
		{
			Inside[InsideCount++] = Index;
		}
		else
		{
			Outside[OutsideCount++] = Index;
		}
	}
	if (InsideCount == 0 || InsideCount == 4)
	{
		return;
	}

	auto EdgePoint = [&Positions, &Fields](int32 A, int32 B)
	{
		return InterpolateIsoPoint5(Positions[A], Positions[B], Fields[A], Fields[B]);
	};
	auto AddTriangle = [this, &OutTriangles](FVector A, FVector B, FVector C)
	{
		FVector3f NormalA = EvaluateSurfaceNormal(A);
		FVector3f NormalB = EvaluateSurfaceNormal(B);
		FVector3f NormalC = EvaluateSurfaceNormal(C);
		const FVector FaceNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		const FVector ExpectedNormal = FVector(NormalA + NormalB + NormalC).GetSafeNormal();
		if (FVector::DotProduct(FaceNormal, ExpectedNormal) < 0.0)
		{
			Swap(B, C);
			Swap(NormalB, NormalC);
		}

		FCachedSurfaceTriangle& Triangle = OutTriangles.Emplace_GetRef();
		Triangle.A = A;
		Triangle.B = B;
		Triangle.C = C;
		Triangle.NormalA = NormalA;
		Triangle.NormalB = NormalB;
		Triangle.NormalC = NormalC;
		Triangle.MaterialIndex = EvaluateSurfaceMaterial((A + B + C) / 3.0);
	};

	if (InsideCount == 1)
	{
		const FVector A = EdgePoint(Inside[0], Outside[0]);
		const FVector B = EdgePoint(Inside[0], Outside[1]);
		const FVector C = EdgePoint(Inside[0], Outside[2]);
		AddTriangle(A, B, C);
	}
	else if (InsideCount == 3)
	{
		const FVector A = EdgePoint(Outside[0], Inside[0]);
		const FVector B = EdgePoint(Outside[0], Inside[1]);
		const FVector C = EdgePoint(Outside[0], Inside[2]);
		AddTriangle(A, B, C);
	}
	else
	{
		const FVector AC = EdgePoint(Inside[0], Outside[0]);
		const FVector AD = EdgePoint(Inside[0], Outside[1]);
		const FVector BC = EdgePoint(Inside[1], Outside[0]);
		const FVector BD = EdgePoint(Inside[1], Outside[1]);
		AddTriangle(AC, BC, BD);
		AddTriangle(AC, BD, AD);
	}
}

void AChunkedDestructibleHalloweenPumpkin5::BuildCellSurfaceCache(
	FIntPoint Cell,
	int32 X,
	int32 Y,
	int32 Z) const
{
	const int32 CellIndex = ToCellIndex(Cell);
	if (!CachedCellSurfaceTriangles.IsValidIndex(CellIndex) ||
		!CachedCellSurfaceBuilt.IsValidIndex(CellIndex) ||
		CachedCellSurfaceBuilt[CellIndex])
	{
		return;
	}

	TArray<FCachedSurfaceTriangle>& Triangles = CachedCellSurfaceTriangles[CellIndex];
	Triangles.Reset();
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const int32 Steps = FMath::Clamp(SurfaceSubdivisions, 1, 4);
	const int32 Tetrahedra[6][4] = {
		{0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6},
		{0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6}
	};
	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	const FVector CellMin = Center - Pitch * 0.5;
	for (int32 SubZ = 0; SubZ < Steps; ++SubZ)
	{
		for (int32 SubY = 0; SubY < Steps; ++SubY)
		{
			for (int32 SubX = 0; SubX < Steps; ++SubX)
			{
				const FVector SubMin = CellMin + FVector(
					Pitch.X * SubX / Steps,
					Pitch.Y * SubY / Steps,
					Pitch.Z * SubZ / Steps);
				const FVector SubSize = Pitch / static_cast<double>(Steps);
				const FVector Positions[8] = {
					SubMin,
					SubMin + FVector(SubSize.X, 0.0, 0.0),
					SubMin + FVector(SubSize.X, SubSize.Y, 0.0),
					SubMin + FVector(0.0, SubSize.Y, 0.0),
					SubMin + FVector(0.0, 0.0, SubSize.Z),
					SubMin + FVector(SubSize.X, 0.0, SubSize.Z),
					SubMin + SubSize,
					SubMin + FVector(0.0, SubSize.Y, SubSize.Z)
				};
				double Fields[8];
				for (int32 Corner = 0; Corner < 8; ++Corner)
				{
					Fields[Corner] = EvaluateSolidField(Positions[Corner]);
				}
				for (const auto& Tetrahedron : Tetrahedra)
				{
					AppendMarchingTetrahedron(Triangles, Positions, Fields, Tetrahedron);
				}
			}
		}
	}
	CachedCellSurfaceBuilt[CellIndex] = true;
}

void AChunkedDestructibleHalloweenPumpkin5::AppendDestroyedNeighborCaps(
	FDynamicMesh3& Mesh,
	int32 X,
	int32 Y,
	int32 Z,
	const FVector& CellCenter) const
{
	if (!bCapDestroyedCellEdges)
	{
		return;
	}
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const int32 Steps = FMath::Clamp(SurfaceSubdivisions, 1, 4);
	struct FDirection
	{
		FIntVector Neighbor;
		FVector Normal;
		int32 Axis;
		double Sign;
	};
	const FDirection Directions[6] = {
		{FIntVector(-1, 0, 0), FVector(-1, 0, 0), 0, -1.0},
		{FIntVector(1, 0, 0), FVector(1, 0, 0), 0, 1.0},
		{FIntVector(0, -1, 0), FVector(0, -1, 0), 1, -1.0},
		{FIntVector(0, 1, 0), FVector(0, 1, 0), 1, 1.0},
		{FIntVector(0, 0, -1), FVector(0, 0, -1), 2, -1.0},
		{FIntVector(0, 0, 1), FVector(0, 0, 1), 2, 1.0}
	};

	for (const FDirection& Direction : Directions)
	{
		const int32 NX = X + Direction.Neighbor.X;
		const int32 NY = Y + Direction.Neighbor.Y;
		const int32 NZ = Z + Direction.Neighbor.Z;
		if (!CellBelongsToShape3D(NX, NY, NZ) || CellExists3D(NX, NY, NZ))
		{
			continue;
		}

		FVector AxisU;
		FVector AxisV;
		FVector FaceCenter = CellCenter;
		if (Direction.Axis == 0)
		{
			FaceCenter.X += Direction.Sign * Pitch.X * 0.5;
			AxisU = FVector(0.0, Pitch.Y, 0.0);
			AxisV = FVector(0.0, 0.0, Pitch.Z);
		}
		else if (Direction.Axis == 1)
		{
			FaceCenter.Y += Direction.Sign * Pitch.Y * 0.5;
			AxisU = FVector(Pitch.X, 0.0, 0.0);
			AxisV = FVector(0.0, 0.0, Pitch.Z);
		}
		else
		{
			FaceCenter.Z += Direction.Sign * Pitch.Z * 0.5;
			AxisU = FVector(Pitch.X, 0.0, 0.0);
			AxisV = FVector(0.0, Pitch.Y, 0.0);
		}

		auto PointAt = [&FaceCenter, &AxisU, &AxisV, Steps](int32 U, int32 V)
		{
			return FaceCenter + AxisU * (static_cast<double>(U) / Steps - 0.5) +
				AxisV * (static_cast<double>(V) / Steps - 0.5);
		};
		const FVector3f FlatNormal(Direction.Normal);
		for (int32 V = 0; V < Steps; ++V)
		{
			for (int32 U = 0; U < Steps; ++U)
			{
				const FVector Quad[4] = {
					PointAt(U, V), PointAt(U + 1, V),
					PointAt(U + 1, V + 1), PointAt(U, V + 1)
				};
				const int32 TriangleIndices[2][3] = {{0, 1, 2}, {0, 2, 3}};
				for (const auto& Triangle : TriangleIndices)
				{
					TArray<TPair<FVector, double>, TInlineAllocator<4>> Polygon;
					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						const FVector& Point = Quad[Triangle[Corner]];
						Polygon.Emplace(Point, EvaluateSolidField(Point));
					}
					TArray<TPair<FVector, double>, TInlineAllocator<4>> Clipped;
					for (int32 Index = 0; Index < Polygon.Num(); ++Index)
					{
						const TPair<FVector, double>& Current = Polygon[Index];
						const TPair<FVector, double>& Previous = Polygon[(Index + Polygon.Num() - 1) % Polygon.Num()];
						const bool bCurrentInside = Current.Value >= 0.0;
						const bool bPreviousInside = Previous.Value >= 0.0;
						if (bCurrentInside != bPreviousInside)
						{
							Clipped.Emplace(
								InterpolateIsoPoint5(Previous.Key, Current.Key, Previous.Value, Current.Value),
								0.0);
						}
						if (bCurrentInside)
						{
							Clipped.Add(Current);
						}
					}
					for (int32 PointIndex = 1; PointIndex + 1 < Clipped.Num(); ++PointIndex)
					{
						AppendSmoothTriangle(
							Mesh,
							Clipped[0].Key, Clipped[PointIndex].Key, Clipped[PointIndex + 1].Key,
							FlatNormal, FlatNormal, FlatNormal,
							InteriorMaterialSlot);
					}
				}
			}
		}
	}
}

void AChunkedDestructibleHalloweenPumpkin5::BuildChunkMesh(
	int32 ChunkIndex,
	FDynamicMesh3& OutMesh) const
{
	OutMesh.EnableVertexNormals(FVector3f::UnitZ());
	OutMesh.EnableAttributes();
	OutMesh.Attributes()->SetNumUVLayers(1);
	OutMesh.Attributes()->EnableMaterialID();
	if (ChunkCount.X <= 0 || SpatialChunkCount.X <= 0 || SpatialChunkCount.Y <= 0)
	{
		return;
	}

	const int32 Side = GetEffectiveSpatialChunkSideLength();
	const int32 ChunkZ = ChunkIndex / ChunkCount.X;
	const int32 PlanarChunk = ChunkIndex % ChunkCount.X;
	const int32 ChunkX = PlanarChunk % SpatialChunkCount.X;
	const int32 ChunkY = PlanarChunk / SpatialChunkCount.X;
	const int32 MinX = ChunkX * Side;
	const int32 MinY = ChunkY * Side;
	const int32 MinZ = ChunkZ * Side;
	const int32 MaxX = FMath::Min(MinX + Side, PumpkinGridCellCount.X);
	const int32 MaxY = FMath::Min(MinY + Side, PumpkinGridCellCount.Y);
	const int32 MaxZ = FMath::Min(MinZ + Side, PumpkinGridCellCount.Z);

	for (int32 Z = MinZ; Z < MaxZ; ++Z)
	{
		for (int32 Y = MinY; Y < MaxY; ++Y)
		{
			for (int32 X = MinX; X < MaxX; ++X)
			{
				const FIntPoint Cell = EncodeCell(X, Y, Z);
				if (Cell == FIntPoint::NoneValue || !Cells[ToCellIndex(Cell)].bExists)
				{
					continue;
				}

				const int32 CellIndex = ToCellIndex(Cell);
				BuildCellSurfaceCache(Cell, X, Y, Z);
				if (CachedCellSurfaceTriangles.IsValidIndex(CellIndex))
				{
					for (const FCachedSurfaceTriangle& Triangle : CachedCellSurfaceTriangles[CellIndex])
					{
						AppendSmoothTriangle(
							OutMesh,
							Triangle.A, Triangle.B, Triangle.C,
							Triangle.NormalA, Triangle.NormalB, Triangle.NormalC,
							Triangle.MaterialIndex);
					}
				}
				AppendDestroyedNeighborCaps(
					OutMesh, X, Y, Z, GetPhysicalCellLocalCenter(X, Y, Z));
			}
		}
	}
}

void AChunkedDestructibleHalloweenPumpkin5::BuildChunkSimpleCollision(
	int32 ChunkIndex,
	FKAggregateGeom& OutCollision) const
{
	OutCollision.EmptyElements();
	if (!bGenerateCollision || ChunkCount.X <= 0 ||
		SpatialChunkCount.X <= 0 || SpatialChunkCount.Y <= 0)
	{
		return;
	}

	const int32 Side = GetEffectiveSpatialChunkSideLength();
	const int32 ChunkZ = ChunkIndex / ChunkCount.X;
	const int32 PlanarChunk = ChunkIndex % ChunkCount.X;
	const int32 ChunkX = PlanarChunk % SpatialChunkCount.X;
	const int32 ChunkY = PlanarChunk / SpatialChunkCount.X;
	const int32 MinX = ChunkX * Side;
	const int32 MinY = ChunkY * Side;
	const int32 MinZ = ChunkZ * Side;
	const int32 MaxX = FMath::Min(MinX + Side, PumpkinGridCellCount.X);
	const int32 MaxY = FMath::Min(MinY + Side, PumpkinGridCellCount.Y);
	const int32 MaxZ = FMath::Min(MinZ + Side, PumpkinGridCellCount.Z);
	DestructibleCollision::AppendBoxRuns(
		OutCollision, FIntVector(MinX, MinY, MinZ), FIntVector(MaxX, MaxY, MaxZ),
		BlockDimensions, BlockSpacing, FVector(PumpkinGridLocalMin.X, PumpkinGridLocalMin.Y, 0.0),
		[this](int32 X, int32 Y, int32 Z) { return CellExists3D(X, Y, Z); });
}

void AChunkedDestructibleHalloweenPumpkin5::ConfigureChunkComponent(
	UDynamicMeshComponent* Component) const
{
	Super::ConfigureChunkComponent(Component);
	if (Component)
	{
		Component->bUseAsyncCooking = bUseAsyncCollisionCooking;
	}
}

void AChunkedDestructibleHalloweenPumpkin5::MarkCellChunksDirty(FIntPoint Cell)
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z) || SpatialChunkCount.X <= 0 ||
		SpatialChunkCount.Y <= 0 || SpatialChunkCount.Z <= 0)
	{
		return;
	}

	const int32 Side = GetEffectiveSpatialChunkSideLength();
	auto MarkSpatialChunk = [this, Side](int32 CellX, int32 CellY, int32 CellZ)
	{
		if (CellX < 0 || CellY < 0 || CellZ < 0 ||
			CellX >= PumpkinGridCellCount.X ||
			CellY >= PumpkinGridCellCount.Y ||
			CellZ >= PumpkinGridCellCount.Z)
		{
			return;
		}
		const int32 ChunkX = CellX / Side;
		const int32 ChunkY = CellY / Side;
		const int32 ChunkZ = CellZ / Side;
		const int32 PlanarChunk = ChunkX + ChunkY * SpatialChunkCount.X;
		DirtyChunkIndices.Add(PlanarChunk + ChunkZ * ChunkCount.X);
	};

	MarkSpatialChunk(X, Y, Z);
	MarkSpatialChunk(X - 1, Y, Z);
	MarkSpatialChunk(X + 1, Y, Z);
	MarkSpatialChunk(X, Y - 1, Z);
	MarkSpatialChunk(X, Y + 1, Z);
	MarkSpatialChunk(X, Y, Z - 1);
	MarkSpatialChunk(X, Y, Z + 1);
}
