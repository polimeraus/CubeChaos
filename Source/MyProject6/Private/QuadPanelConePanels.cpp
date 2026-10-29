#include "QuadPanelConeStructure.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UObject/UnrealType.h"

using namespace UE::Geometry;

void AQuadPanelConeStructure::CopySettingsFromCone(AChunkedDestructibleConeStructure* Source)
{
	if (!IsValid(Source)) { return; }
	for (TFieldIterator<FProperty> It(Source->GetClass()); It; ++It)
	{
		FProperty* Original = *It;
		const UClass* OwnerClass = Original->GetOwnerClass();
		if (!OwnerClass || !OwnerClass->IsChildOf(AChunkedDestructibleFloor::StaticClass()) ||
			!Original->HasAnyPropertyFlags(CPF_Edit) || Original->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_InstancedReference)) { continue; }
		FProperty* Destination = FindFProperty<FProperty>(GetClass(), Original->GetFName());
		if (Destination && Destination->SameType(Original))
		{
			Destination->CopyCompleteValue(Destination->ContainerPtrToValuePtr<void>(this),
				Original->ContainerPtrToValuePtr<void>(Source));
		}
	}
	// Whole platform slabs complement the regular four-sided wall panels.
	SpiralDestructionMode = EConeStructureSpiralDestructionMode::OneSegmentPerHit;
	bUseSmoothComplexCollision = true;
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RebuildConeStructure(false);
		RebuildApexDecoration();
	}
}

bool AQuadPanelConeStructure::IsPanelCell(FIntPoint Cell) const
{
	return Cell.Y >= 0 && Cell.Y < GridHeightBlocks && Cell.X >= PanelCellOffset &&
		Cell.X < PanelCellOffset + WallPanelSegments && CellCount.X > PanelCellOffset;
}

FIntPoint AQuadPanelConeStructure::PanelCell(int32 Column, int32 Row) const
{
	return FIntPoint(PanelCellOffset + (Column % WallPanelSegments + WallPanelSegments) % WallPanelSegments, Row);
}

int32 AQuadPanelConeStructure::PanelChunk(FIntPoint Cell) const
{
	return PanelChunkStart + (Cell.X - PanelCellOffset) / 8 + (Cell.Y / 4) * PanelChunkColumns;
}

void AQuadPanelConeStructure::GetPanelCorners(FIntPoint Cell, FVector3d (&V)[8]) const
{
	const double Pitch = BlockDimensions.Z + BlockSpacing.Z;
	const double Height = BlockDimensions.Z * GridHeightBlocks + BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	const double Seal = BlockSpacing.Z * .5 * FMath::Clamp(SurfaceGapSealFraction, 0.f, 1.f);
	const double Low = FMath::Max(Cell.Y * Pitch - Seal, 0.) + (Cell.Y > 0 ? PanelJointWidth * .5 : 0.);
	const double High = FMath::Min(Cell.Y * Pitch + BlockDimensions.Z + Seal, Height) -
		(Cell.Y + 1 < GridHeightBlocks ? PanelJointWidth * .5 : 0.);
	const double Step = UE_DOUBLE_TWO_PI / WallPanelSegments;
	const double Radius = GetConeOuterRadiusAtHeight((Low + High) * .5);
	const double GapAngle = FMath::Min(double(PanelJointWidth) / FMath::Max(Radius, 1.), Step * .15);
	const double A = (Cell.X - PanelCellOffset) * Step + GapAngle * .5;
	const double B = (Cell.X - PanelCellOffset + 1) * Step - GapAngle * .5;
	const FVector3d RA(FMath::Cos(A), FMath::Sin(A), 0), RB(FMath::Cos(B), FMath::Sin(B), 0);
	V[0] = RA * GetConeOuterRadiusAtHeight(Low) + FVector3d(0, 0, Low);
	V[1] = RB * GetConeOuterRadiusAtHeight(Low) + FVector3d(0, 0, Low);
	V[2] = RB * GetConeOuterRadiusAtHeight(High) + FVector3d(0, 0, High);
	V[3] = RA * GetConeOuterRadiusAtHeight(High) + FVector3d(0, 0, High);
	V[4] = RA * GetConeInnerRadiusAtHeight(Low) + FVector3d(0, 0, Low);
	V[5] = RB * GetConeInnerRadiusAtHeight(Low) + FVector3d(0, 0, Low);
	V[6] = RB * GetConeInnerRadiusAtHeight(High) + FVector3d(0, 0, High);
	V[7] = RA * GetConeInnerRadiusAtHeight(High) + FVector3d(0, 0, High);
}

bool AQuadPanelConeStructure::IsPanelInDoor(FIntPoint Cell) const
{
	if (!bGenerateEntrance) { return false; }
	const FVector Center = GetCellLocalCenter(Cell);
	const int32 X = FMath::FloorToInt((Center.X - ConeGridLocalMin.X) / (BlockDimensions.X + BlockSpacing.X));
	const int32 Y = FMath::FloorToInt((Center.Y - ConeGridLocalMin.Y) / (BlockDimensions.Y + BlockSpacing.Y));
	return IsEntranceCell(X, Y, Cell.Y);
}

bool AQuadPanelConeStructure::FindPanelAtLocalPoint(const FVector& Point, FIntPoint& OutCell, double Tolerance) const
{
	if (GridHeightBlocks <= 0 || CellCount.X <= PanelCellOffset || Point.ContainsNaN()) { return false; }
	const double Height = BlockDimensions.Z * GridHeightBlocks + BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	if (Point.Z < -Tolerance || Point.Z > Height + Tolerance) { return false; }
	double Angle = FMath::Atan2(Point.Y, Point.X);
	if (Angle < 0.) { Angle += UE_DOUBLE_TWO_PI; }
	const int32 Column = FMath::Min(FMath::FloorToInt(Angle / UE_DOUBLE_TWO_PI * WallPanelSegments), WallPanelSegments - 1);
	const int32 Row = FMath::Clamp(FMath::FloorToInt(Point.Z / (BlockDimensions.Z + BlockSpacing.Z)), 0, GridHeightBlocks - 1);
	const FIntPoint Cell = PanelCell(Column, Row);
	FVector3d V[8]; GetPanelCorners(Cell, V);
	if (Point.Z < V[0].Z - Tolerance || Point.Z > V[2].Z + Tolerance || IsPanelInDoor(Cell)) { return false; }
	const double Mid = (Column + .5) * UE_DOUBLE_TWO_PI / WallPanelSegments;
	const FVector Axis(FMath::Cos(Mid), FMath::Sin(Mid), 0);
	const double SideAngle = FMath::Atan2(V[0].Y, V[0].X);
	const double CosHalf = FMath::Cos(FMath::UnwindRadians(SideAngle - Mid));
	const double Distance = FVector::DotProduct(Point, Axis);
	const double Outer = GetConeOuterRadiusAtHeight(Point.Z) * CosHalf;
	const double Inner = GetConeInnerRadiusAtHeight(Point.Z) * CosHalf;
	if (Distance < Inner - Tolerance || Distance > Outer + Tolerance) { return false; }
	const double HalfAngle = FMath::Acos(FMath::Clamp(CosHalf, -1., 1.));
	if (FMath::Abs(FMath::UnwindRadians(Angle - Mid)) > HalfAngle + Tolerance / FMath::Max(Outer, 1.)) { return false; }
	OutCell = Cell;
	return true;
}

void AQuadPanelConeStructure::AppendPanel(FDynamicMesh3& Mesh, FIntPoint Cell, int32 Material, float SurfaceOffset) const
{
	FVector3d V[8]; GetPanelCorners(Cell, V);
	for (int32 Index = 0; Index < 8 && SurfaceOffset > 0.f; ++Index)
	{
		const FVector3d Radial = FVector3d(V[Index].X, V[Index].Y, 0).GetSafeNormal();
		V[Index] += Radial * SurfaceOffset * (Index < 4 ? 1. : -1.);
	}
	FVector3d Center = FVector3d::Zero();
	for (const FVector3d& Point : V) { Center += Point / 8.; }
	const int32 First = Mesh.MaxTriangleID();
	const double Smoothness = FMath::IsFinite(PanelShadingSmoothness) ? FMath::Clamp(PanelShadingSmoothness, 0.f, 1.f) : 1.0;
	const double Height = FMath::Max(BlockDimensions.Z * GridHeightBlocks +
		BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0), 1.0);
	const double NormalZ = (BottomRadiusBlocks - EffectiveTopRadiusBlocks) * CachedRadialBlockUnit / Height;
	// Each piece has six planar faces. A face consists of two render triangles,
	// but ownership, collision and destruction always operate on the whole panel.
	auto Face = [&](int32 IA, int32 IB, int32 IC, int32 ID, double ShellSide = 0.)
	{
		FVector3d A = V[IA], B = V[IB], C = V[IC], D = V[ID];
		FVector3d Normal = (B - A).Cross(C - A) + (C - A).Cross(D - A);
		if (!Normal.Normalize()) { return; }
		if (Normal.Dot((A + B + C + D) * .25 - Center) < 0.) { Swap(B, D); Normal = -Normal; }
		const int32 FirstVertex = Mesh.MaxVertexID();
		const int32 FirstFaceTriangle = Mesh.MaxTriangleID();
		if ((B - A).Cross(C - A).SquaredLength() > 1.e-8 && (C - A).Cross(D - A).SquaredLength() > 1.e-8)
		{
			AppendMeshQuad(Mesh, A, B, C, D, FVector3f(Normal), Material,
				(B - A).Length(), (D - A).Length(), UVWorldUnits);
		}
		else
		{
			if ((B - A).Cross(C - A).SquaredLength() > 1.e-8)
				AppendMeshTriangle(Mesh, A, B, C, FVector3f(Normal), Material, FVector2f(0,0), FVector2f(1,0), FVector2f(1,1));
			if ((C - A).Cross(D - A).SquaredLength() > 1.e-8)
				AppendMeshTriangle(Mesh, A, C, D, FVector3f(Normal), Material, FVector2f(0,0), FVector2f(1,1), FVector2f(0,1));
		}
		if (ShellSide == 0. || Smoothness <= 0.) { return; }
		// An analytic normal agrees across panel and chunk boundaries without a
		// neighbor search or welded geometry. End caps and destruction cuts stay flat.
		for (int32 Vertex = FirstVertex; Vertex < Mesh.MaxVertexID(); ++Vertex)
		{
			const FVector3d Point = Mesh.GetVertex(Vertex);
			FVector3d SmoothNormal = FVector3d(Point.X, Point.Y, 0).GetSafeNormal();
			SmoothNormal = SmoothNormal.IsNearlyZero() ? FVector3d::UnitZ() :
				FVector3d(SmoothNormal.X, SmoothNormal.Y, NormalZ).GetSafeNormal();
			Mesh.SetVertexNormal(Vertex, FVector3f(FMath::Lerp(Normal, SmoothNormal * ShellSide, Smoothness).GetSafeNormal()));
		}
		if (Mesh.HasAttributes())
		{
			if (FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals())
			{
				for (int32 Triangle = FirstFaceTriangle; Triangle < Mesh.MaxTriangleID(); ++Triangle)
				{
					const FIndex3i Vertices = Mesh.GetTriangle(Triangle), Elements = Normals->GetTriangle(Triangle);
					for (int32 Corner = 0; Corner < 3; ++Corner) { Normals->SetElement(Elements[Corner], Mesh.GetVertexNormal(Vertices[Corner])); }
				}
			}
		}
	};
	Face(0, 1, 2, 3, 1.); Face(4, 7, 6, 5, -1.);
	Face(0, 4, 5, 1); Face(3, 2, 6, 7);
	Face(0, 3, 7, 4); Face(1, 5, 6, 2);
	SetMusicTriangleCells(Mesh, First, Cell);
}

void AQuadPanelConeStructure::AppendPanelChunk(FDynamicMesh3& Mesh, int32 ChunkIndex) const
{
	const int32 Index = ChunkIndex - PanelChunkStart;
	if (Index < 0 || PanelChunkColumns <= 0) { return; }
	const int32 FirstRow = (Index / PanelChunkColumns) * 4;
	const int32 FirstColumn = (Index % PanelChunkColumns) * 8;
	for (int32 Row = FirstRow; Row < FMath::Min(FirstRow + 4, GridHeightBlocks); ++Row)
	{
		for (int32 Column = FirstColumn; Column < FMath::Min(FirstColumn + 8, WallPanelSegments); ++Column)
		{
			const FIntPoint Cell = PanelCell(Column, Row);
			if (!Cells.IsValidIndex(ToCellIndex(Cell)) || !Cells[ToCellIndex(Cell)].bExists) { continue; }
			const int32 FirstTriangle = Mesh.MaxTriangleID();
			AppendPanel(Mesh, Cell);
			// Both triangles of the outer trapezoid participate in traveling music highlights.
			if (Mesh.IsTriangle(FirstTriangle)) { MusicChunkShellTriangles.FindOrAdd(ChunkIndex).Add(FirstTriangle); }
			if (Mesh.IsTriangle(FirstTriangle + 1)) { MusicChunkShellTriangles.FindOrAdd(ChunkIndex).Add(FirstTriangle + 1); }
		}
	}
}

void AQuadPanelConeStructure::AddPanelsInRadius(FVector WorldLocation, float Radius, EChunkedFloorBlastShape Shape,
	TFunctionRef<void(int32, float)> CellFunction, int32& Count) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const double LocalRadius = Radius / FMath::Max(FMath::Abs(GetActorScale3D().Z), .001);
	const double Pitch = BlockDimensions.Z + BlockSpacing.Z;
	const int32 First = FMath::Clamp(FMath::FloorToInt((Local.Z - LocalRadius) / Pitch), 0, GridHeightBlocks - 1);
	const int32 Last = FMath::Clamp(FMath::FloorToInt((Local.Z + LocalRadius) / Pitch), 0, GridHeightBlocks - 1);
	for (int32 Row = First; Row <= Last; ++Row)
	{
		for (int32 Column = 0; Column < WallPanelSegments; ++Column)
		{
			const FIntPoint Cell = PanelCell(Column, Row);
			const int32 Index = ToCellIndex(Cell);
			if (!Cells.IsValidIndex(Index) || !Cells[Index].bExists) { continue; }
			FVector Point = GetCellLocalCenter(Cell);
			if (Shape == EChunkedFloorBlastShape::Sphere)
			{
				FVector3d Corners[8]; GetPanelCorners(Cell, Corners);
				FBox Bounds(ForceInit);
				for (const FVector3d& Corner : Corners) { Bounds += Corner; }
				Point = Bounds.GetClosestPointTo(Local);
			}
			const FVector Delta = GetActorTransform().TransformPosition(Point) - WorldLocation;
			const float Distance = Shape == EChunkedFloorBlastShape::Square ? Delta.GetAbsMax() :
				Shape == EChunkedFloorBlastShape::Diamond ? Delta.GetAbs().X + Delta.GetAbs().Y + Delta.GetAbs().Z : Delta.Length();
			if (Distance <= Radius) { CellFunction(Index, Distance); ++Count; }
		}
	}
}
