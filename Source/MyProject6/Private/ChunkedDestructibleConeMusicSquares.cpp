#include "ChunkedDestructibleConeStructure.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace UE::Geometry;

namespace
{
	// Clip the existing shell triangles, so neighboring destruction cells share one
	// straight square edge. No collision geometry or destructible cell is resized.
	void ClipMusicSquare(TArray<FVector3d>& Polygon, const FVector3d& Axis, double Limit)
	{
		if (Polygon.IsEmpty()) { return; }
		TArray<FVector3d> Result;
		FVector3d Previous = Polygon.Last();
		double PreviousDistance = Previous.Dot(Axis) - Limit;
		for (const FVector3d& Point : Polygon)
		{
			const double Distance = Point.Dot(Axis) - Limit;
			if ((Distance <= 0) != (PreviousDistance <= 0))
			{
				Result.Add(FMath::Lerp(Previous, Point, PreviousDistance / (PreviousDistance - Distance)));
			}
			if (Distance <= 0) { Result.Add(Point); }
			Previous = Point;
			PreviousDistance = Distance;
		}
		Polygon = MoveTemp(Result);
	}
}

void AChunkedDestructibleConeStructure::BuildMusicSquareRows(float Size)
{
	const double Height = FMath::Max(BlockDimensions.Z * GridHeightBlocks +
		BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0), 1.0);
	const double Slope = (TopRadiusBlocks - BottomRadiusBlocks) * CachedRadialBlockUnit / Height;
	const double SurfaceScale = FMath::Sqrt(1.0 + Slope * Slope);
	// Bound layout work for extreme values while retaining the requested size normally.
	const double TargetSide = FMath::Max3(double(Size), Height * SurfaceScale / 128.0,
		UE_TWO_PI * FMath::Max(GetConeOuterRadiusAtHeight(0), GetConeOuterRadiusAtHeight(Height)) / 256.0);
	constexpr double CurvatureLimit = 0.8;
	const double TaperCorrection = 1.0 + CurvatureLimit * FMath::Abs(Slope) / SurfaceScale;
	const double MinimumSide = FMath::Min(TargetSide,
		GetConeOuterRadiusAtHeight(0) * CurvatureLimit / TaperCorrection) / 8.0;
	double Z = 0;
	while (Z < Height && MusicSquareRows.Num() < 512)
	{
		const double Radius = GetConeOuterRadiusAtHeight(Z);
		// Limit curvature and taper across a square. Near a true apex, stop before
		// the final partial tile instead of drawing a tiny triangle or a collapsed wedge.
		double Side = FMath::Min(TargetSide, Radius * CurvatureLimit / TaperCorrection);
		Side = FMath::Min(Side, (Height - Z) * SurfaceScale);
		if (Side < MinimumSide) { break; }
		const double MaxZ = Z + Side / SurfaceScale;
		const double MinRadius = FMath::Min(Radius, double(GetConeOuterRadiusAtHeight(MaxZ)));
		if (MinRadius <= Side * 0.5) { break; }
		FMusicSquareRow& Row = MusicSquareRows.Emplace_GetRef();
		Row.MinZ = Z;
		Row.MaxZ = MaxZ;
		Row.Side = Side;
		// The tangent chord fits even at the narrow end of the row. This leaves
		// space between tiles rather than squeezing their upper edges together.
		Row.Columns = FMath::Clamp(FMath::FloorToInt(UE_PI / FMath::Asin(Side / (2.0 * MinRadius))), 3, 256);
		Row.FirstGroup = MusicGlowGroups.Num();
		MusicGlowGroups.AddDefaulted(Row.Columns);
		Z = MaxZ;
	}
}

void AChunkedDestructibleConeStructure::CacheMusicSquareChunk(int32 Chunk)
{
	auto& Groups = MusicSquareChunkTriangles.Add(Chunk);
	const auto* ShellTriangles = MusicChunkShellTriangles.Find(Chunk);
	if (!ShellTriangles || !ChunkComponents.IsValidIndex(Chunk) || !ChunkComponents[Chunk]) { return; }
	const FDynamicMesh3* Mesh = ChunkComponents[Chunk]->GetMesh();
	if (!Mesh) { return; }
	for (int32 Triangle : *ShellTriangles)
	{
		if (!Mesh->IsTriangle(Triangle)) { continue; }
		FVector3d A, B, C;
		// AppendMeshTriangle accepts counter-clockwise corners and reverses them
		// for Unreal. Undo the source mesh winding before clipping and re-emitting.
		Mesh->GetTriVertices(Triangle, A, C, B);
		const int32 CellIndex = Mesh->GetTriangleGroup(Triangle);
		const double MinZ = FMath::Min3(A.Z, B.Z, C.Z);
		const double MaxZ = FMath::Max3(A.Z, B.Z, C.Z);
		const FVector3d Center = (A + B + C) / 3.0;
		for (const FMusicSquareRow& Row : MusicSquareRows)
		{
			// Equal gaps in both surface directions keep individual squares readable.
			const double HalfSide = Row.Side * 0.46;
			const double Low = FMath::Lerp(Row.MinZ, Row.MaxZ, 0.04);
			const double High = FMath::Lerp(Row.MinZ, Row.MaxZ, 0.96);
			if (MinZ > High || MaxZ < Low) { continue; }
			for (int32 Column = 0; Column < Row.Columns; ++Column)
			{
				const double Angle = -UE_PI + (Column + 0.5) * UE_TWO_PI / Row.Columns;
				const FVector3d Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0);
				if (Center.Dot(Radial) <= 0) { continue; }
				const FVector3d Tangent(-Radial.Y, Radial.X, 0);
				if (FMath::Min3(A.Dot(Tangent), B.Dot(Tangent), C.Dot(Tangent)) > HalfSide ||
					FMath::Max3(A.Dot(Tangent), B.Dot(Tangent), C.Dot(Tangent)) < -HalfSide) { continue; }
				TArray<FVector3d> Polygon = { A, B, C };
				ClipMusicSquare(Polygon, FVector3d::UnitZ(), High);
				ClipMusicSquare(Polygon, -FVector3d::UnitZ(), -Low);
				ClipMusicSquare(Polygon, Tangent, HalfSide);
				ClipMusicSquare(Polygon, -Tangent, HalfSide);
				for (int32 Point = 1; Point + 1 < Polygon.Num(); ++Point)
				{
					const FVector3d Normal = (Polygon[Point] - Polygon[0]).Cross(Polygon[Point + 1] - Polygon[0]);
					if (Normal.SquaredLength() < 1.e-10) { continue; }
					Groups.FindOrAdd(Row.FirstGroup + Column).Add({ Polygon[0], Polygon[Point], Polygon[Point + 1], CellIndex });
				}
			}
		}
	}
}

void AChunkedDestructibleConeStructure::RenderMusicSurfaceSquares(const TSet<int32>& SelectedGroups)
{
	if (SelectedGroups.IsEmpty())
	{
		MusicHighlightedSquareGroups.Reset();
		if (MusicSquareComponent) { MusicSquareComponent->SetVisibility(false); }
		return;
	}
	// Once on enabling music, collect ownership from all chunks. A square can cross
	// both a cell boundary and a chunk boundary; neither should interrupt its edges.
	for (int32 Chunk = 0; Chunk < ChunkComponents.Num(); ++Chunk)
	{
		if (!MusicChunkOriginalMaterials.Contains(Chunk)) { RebuildChunk(Chunk); }
		if (!MusicSquareChunkTriangles.Contains(Chunk)) { CacheMusicSquareChunk(Chunk); }
	}
	if (MusicSquareComponent) { MusicSquareComponent->SetMaterial(0, MusicReactionMaterialInstance); }
	if (!bMusicSquareMeshDirty && SelectedGroups.Num() == MusicHighlightedSquareGroups.Num() &&
		SelectedGroups.Difference(MusicHighlightedSquareGroups).IsEmpty()) { return; }
	MusicHighlightedSquareGroups = SelectedGroups;
	bMusicSquareMeshDirty = false;
	if (!MusicSquareComponent)
	{
		MusicSquareComponent = NewObject<UDynamicMeshComponent>(this, NAME_None, RF_Transient);
		AddInstanceComponent(MusicSquareComponent);
		MusicSquareComponent->SetupAttachment(RootComponent);
		MusicSquareComponent->SetRelativeTransform(FTransform::Identity);
		MusicSquareComponent->SetMobility(EComponentMobility::Movable);
		MusicSquareComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MusicSquareComponent->SetGenerateOverlapEvents(false);
		MusicSquareComponent->SetCastShadow(false);
		MusicSquareComponent->SetCanEverAffectNavigation(false);
		MusicSquareComponent->ComponentTags.Add(TEXT("MusicSurfaceSquares"));
		MusicSquareComponent->RegisterComponent();
		MusicSquareComponent->SetMaterial(0, MusicReactionMaterialInstance);
	}
	FDynamicMesh3 Surface;
	Surface.EnableVertexNormals(FVector3f::UnitZ());
	Surface.EnableAttributes();
	Surface.Attributes()->SetNumUVLayers(1);
	Surface.Attributes()->EnableMaterialID();
	for (const auto& Chunk : MusicSquareChunkTriangles)
	{
		for (int32 Group : SelectedGroups)
		{
			const auto* Triangles = Chunk.Value.Find(Group);
			if (!Triangles) { continue; }
			for (const FMusicSquareTriangle& Triangle : *Triangles)
			{
				if (!Cells.IsValidIndex(Triangle.CellIndex) || !Cells[Triangle.CellIndex].bExists ||
					PendingCellIndices.Contains(Triangle.CellIndex)) { continue; }
				const FVector3d Normal = (Triangle.B - Triangle.A).Cross(Triangle.C - Triangle.A).GetSafeNormal();
				// A sub-millimetre surface offset avoids z fighting with the same shell.
				const FVector3d Offset = Normal * 0.05;
				AppendMeshTriangle(Surface, Triangle.A + Offset, Triangle.B + Offset, Triangle.C + Offset,
					FVector3f(Normal), 0, FVector2f::ZeroVector, FVector2f::ZeroVector, FVector2f::ZeroVector);
			}
		}
	}
	const bool bVisible = Surface.TriangleCount() > 0;
	MusicSquareComponent->GetDynamicMesh()->SetMesh(MoveTemp(Surface));
	MusicSquareComponent->NotifyMeshUpdated();
	MusicSquareComponent->SetVisibility(bVisible);
}
