#include "ChunkedDestructibleConeStructure.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace UE::Geometry;

float AChunkedDestructibleConeStructure::GetTravelingColumnsSpeed() const
{
	return FMath::IsFinite(TravelingColumnsSpeed) ? FMath::Clamp(TravelingColumnsSpeed, 0.1f, 4.0f) : 0.65f;
}

void AChunkedDestructibleConeStructure::SetTravelingColumnsSpeed(float NewSpeed)
{
	const float PreviousSpeed = GetTravelingColumnsSpeed();
	TravelingColumnsSpeed = FMath::IsFinite(NewSpeed) ? FMath::Clamp(NewSpeed, 0.1f, 4.0f) : 0.65f;
	if (MusicAnimationType != EMusicAnimationType::TravelingColumns) { return; }
	const double Now = GetMusicAnimationTime();
	for (auto& Column : ActiveMusicEqualizerColumns)
	{
		// Preserve the current height (and future sweep delays) when adjusting speed live.
		const double Ratio = PreviousSpeed / TravelingColumnsSpeed;
		Column.StartTime = Now - (Now - Column.StartTime) * Ratio;
		Column.Duration *= Ratio;
	}
}

void AChunkedDestructibleConeStructure::SetMusicGlowGroupSize(float NewSize)
{
	const float Size = FMath::IsFinite(NewSize) ? FMath::Clamp(NewSize, 1.0f, 10000.0f) : 200.0f;
	if (MusicGlowGroupSize == Size) { return; }
	ResetMusicPlayback();
	MusicGlowGroupSize = Size;
	MusicCellGroupIndices.Empty();
	MusicGlowGroups.Empty();
	CachedMusicGlowGroupSize = 0.0f;
}

FIntVector AChunkedDestructibleConeStructure::GetMusicGlowGroupKey(int32 CellIndex, float GroupSize) const
{
	const FIntPoint Cell = FromCellIndex(CellIndex);
	const FVector Center = GetCellLocalCenter(Cell);
	int32 X, Y, Z;
	if (!DecodeCell(Cell, X, Y, Z)) { return FIntVector(INDEX_NONE); }
	if (MusicAnimationType == EMusicAnimationType::TravelingColumns)
	{
		if (!IsConeShellCell(X, Y, Z) || MusicSquareRows.IsEmpty()) { return FIntVector(INDEX_NONE); }
		int32 RowIndex = 0;
		while (RowIndex + 1 < MusicSquareRows.Num() && Center.Z >= MusicSquareRows[RowIndex].MaxZ) { ++RowIndex; }
		const FMusicSquareRow& Row = MusicSquareRows[RowIndex];
		const double Angle = FMath::Atan2(Center.Y, Center.X) + UE_PI;
		return FIntVector(FMath::FloorToInt(Angle / UE_TWO_PI * Row.Columns) % Row.Columns, RowIndex, INDEX_NONE);
	}
	if (Cells[CellIndex].MaterialIndex == 1 || IsConeBaseCell(X, Y, Z))
	{
		// Horizontal surfaces use a fixed square grid anchored to the structure's footprint.
		return FIntVector(FMath::FloorToInt((Center.X - ConeGridLocalMin.X) / GroupSize),
			FMath::FloorToInt((Center.Y - ConeGridLocalMin.Y) / GroupSize),
			FMath::Max(0, FMath::FloorToInt(Center.Z / GroupSize)));
	}
	const double Height = FMath::Max(BlockDimensions.Z * GridHeightBlocks +
		BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0), 1.0);
	const double Slope = (TopRadiusBlocks - BottomRadiusBlocks) * CachedRadialBlockUnit / Height;
	const double SurfaceScale = FMath::Sqrt(1.0 + Slope * Slope);
	const int32 Row = FMath::Max(0, FMath::FloorToInt(Center.Z * SurfaceScale / GroupSize));
	const double RowCenterZ = FMath::Min((Row + 0.5) * GroupSize / SurfaceScale, Height);
	const double Circumference = UE_TWO_PI * GetConeOuterRadiusAtHeight(RowCenterZ);
	const int32 Columns = FMath::Max(1, FMath::RoundToInt(Circumference / GroupSize));
	const double Angle = FMath::Atan2(Center.Y, Center.X) + UE_PI;
	const int32 Column = FMath::FloorToInt(Angle / UE_TWO_PI * Columns) % Columns;
	// Angular tiles follow the curved shell; vertical height accounts for its slope.
	// The -1 distinguishes shell tiles from the horizontal grid, whose Z is nonnegative.
	return FIntVector(Column, Row, INDEX_NONE);
}

void AChunkedDestructibleConeStructure::EnsureMusicGlowGroups()
{
	const float Size = FMath::IsFinite(MusicGlowGroupSize) ? FMath::Clamp(MusicGlowGroupSize, 1.0f, 10000.0f) : 200.0f;
	if (CachedMusicGlowGroupSize == Size && MusicCellGroupIndices.Num() == Cells.Num() &&
		CachedMusicGroupAnimationType == MusicAnimationType) { return; }
	MusicCellGroupIndices.Init(INDEX_NONE, Cells.Num());
	MusicGlowGroups.Reset();
	MusicSquareRows.Reset();
	MusicSquareChunkTriangles.Empty();
	bMusicSquareMeshDirty = true;
	if (MusicAnimationType == EMusicAnimationType::TravelingColumns) { BuildMusicSquareRows(Size); }
	TMap<FIntVector, int32> GroupsByTile;
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		// Keep destroyed members in the layout: damage must never shift a neighboring tile.
		if (!Cells[CellIndex].bBelongsToGeneratedShape) { continue; }
		const FIntVector Key = GetMusicGlowGroupKey(CellIndex, Size);
		if (Key == FIntVector(INDEX_NONE)) { continue; }
		if (MusicAnimationType == EMusicAnimationType::TravelingColumns)
		{
			const int32 Group = MusicSquareRows[Key.Y].FirstGroup + Key.X;
			MusicCellGroupIndices[CellIndex] = Group;
			MusicGlowGroups[Group].Add(CellIndex);
			continue;
		}
		int32* Existing = GroupsByTile.Find(Key);
		const int32 GroupIndex = Existing ? *Existing : MusicGlowGroups.AddDefaulted();
		if (!Existing) { GroupsByTile.Add(Key, GroupIndex); }
		MusicCellGroupIndices[CellIndex] = GroupIndex;
		MusicGlowGroups[GroupIndex].Add(CellIndex);
	}
	CachedMusicGlowGroupSize = Size;
	CachedMusicGroupAnimationType = MusicAnimationType;
}

FTransform AChunkedDestructibleConeStructure::GetMusicEqualizerTransform(FIntPoint Cell) const
{
	if (MusicAnimationType == EMusicAnimationType::TravelingColumns)
	{
		int32 X, Y, Z;
		if (!DecodeCell(Cell, X, Y, Z) || !IsConeShellCell(X, Y, Z))
		{
			return FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
		}
	}
	// Only logical placement is needed. The original clipped shell supplies the surface.
	return FTransform(GetCellLocalCenter(Cell));
}

void AChunkedDestructibleConeStructure::SetMusicTriangleCells(FDynamicMesh3& Mesh, int32 FirstTriangle, FIntPoint Cell) const
{
	if (!Mesh.HasTriangleGroups()) { return; }
	for (int32 Triangle = FirstTriangle; Triangle < Mesh.MaxTriangleID(); ++Triangle)
	{
		if (Mesh.IsTriangle(Triangle)) { Mesh.SetTriangleGroup(Triangle, ToCellIndex(Cell)); }
	}
}

void AChunkedDestructibleConeStructure::CacheMusicChunkMaterials(int32 ChunkIndex, FDynamicMesh3& Mesh) const
{
	if (!IsMusicReactionEnabled()) { MusicChunkOriginalMaterials.Remove(ChunkIndex); return; }
	MusicSquareChunkTriangles.Remove(ChunkIndex);
	bMusicSquareMeshDirty = true;
	TArray<int32>& Originals = MusicChunkOriginalMaterials.FindOrAdd(ChunkIndex);
	Originals.Init(INDEX_NONE, Mesh.MaxTriangleID());
	auto* Materials = Mesh.Attributes()->GetMaterialID();
	for (int32 Triangle : Mesh.TriangleIndicesItr())
	{
		Originals[Triangle] = Materials->GetValue(Triangle);
		const int32 CellIndex = Mesh.GetTriangleGroup(Triangle);
		if (MusicAnimationType != EMusicAnimationType::TravelingColumns &&
			MusicHighlightedCells.Contains(CellIndex) && Cells.IsValidIndex(CellIndex) &&
			Cells[CellIndex].bExists && !PendingCellIndices.Contains(CellIndex))
		{
			Materials->SetValue(Triangle, MusicMaterialSlot);
		}
	}
}

void AChunkedDestructibleConeStructure::AddMusicAffectedChunks(int32 CellIndex, TSet<int32>& OutChunks) const
{
	int32 X, Y, Z;
	if (!DecodeCell(FromCellIndex(CellIndex), X, Y, Z)) { return; }
	// A newly exposed face can be stored in the adjacent destroyed cell's spatial chunk.
	const FIntVector Offsets[] = { FIntVector::ZeroValue, FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
	for (FIntVector Offset : Offsets)
	{
		const int32 Chunk = GetSpatialChunkIndex(X + Offset.X, Y + Offset.Y, Z + Offset.Z);
		if (ChunkComponents.IsValidIndex(Chunk)) { OutChunks.Add(Chunk); }
	}
}

void AChunkedDestructibleConeStructure::RenderMusicEqualizerCells(TConstArrayView<int32> CellIndices)
{
	TSet<int32> NewHighlights;
	TSet<int32> SelectedGroups;
	if (!CellIndices.IsEmpty()) { EnsureMusicGlowGroups(); }
	for (int32 CellIndex : CellIndices)
	{
		if (Cells.IsValidIndex(CellIndex) && Cells[CellIndex].bExists && !PendingCellIndices.Contains(CellIndex))
		{
			const int32 Group = MusicCellGroupIndices[CellIndex];
			if (MusicGlowGroups.IsValidIndex(Group)) { SelectedGroups.Add(Group); }
		}
	}
	for (int32 Group : SelectedGroups)
	{
		for (int32 CellIndex : MusicGlowGroups[Group])
		{
			if (Cells[CellIndex].bExists && !PendingCellIndices.Contains(CellIndex))
			{
				NewHighlights.Add(CellIndex);
			}
		}
	}
	if (MusicAnimationType == EMusicAnimationType::TravelingColumns)
	{
		if (NewHighlights.Num() != MusicHighlightedCells.Num() ||
			!NewHighlights.Difference(MusicHighlightedCells).IsEmpty()) { bMusicSquareMeshDirty = true; }
		MusicHighlightedCells = MoveTemp(NewHighlights);
		RenderMusicSurfaceSquares(SelectedGroups);
		return;
	}
	TSet<int32> ChangedChunks;
	for (int32 CellIndex : MusicHighlightedCells)
	{
		if (!NewHighlights.Contains(CellIndex)) { AddMusicAffectedChunks(CellIndex, ChangedChunks); }
	}
	for (int32 CellIndex : NewHighlights)
	{
		if (!MusicHighlightedCells.Contains(CellIndex)) { AddMusicAffectedChunks(CellIndex, ChangedChunks); }
	}
	MusicHighlightedCells = MoveTemp(NewHighlights);
	for (int32 Chunk : ChangedChunks)
	{
		UDynamicMeshComponent* Component = ChunkComponents[Chunk];
		if (!Component || !Component->GetMesh()) { continue; }
		const TArray<int32>* Originals = MusicChunkOriginalMaterials.Find(Chunk);
		if (!Originals)
		{
			// Music enabled after construction: record ownership once, then reuse the mesh.
			if (IsMusicReactionEnabled()) { RebuildChunk(Chunk); }
			continue;
		}
		FDynamicMesh3& Mesh = *Component->GetMesh();
		if (MusicReactionMaterialInstance) { Component->SetMaterial(MusicMaterialSlot, MusicReactionMaterialInstance); }
		auto* Materials = Mesh.Attributes()->GetMaterialID();
		bool bChanged = false;
		for (int32 Triangle : Mesh.TriangleIndicesItr())
		{
			const int32 Material = MusicHighlightedCells.Contains(Mesh.GetTriangleGroup(Triangle))
				? MusicMaterialSlot : (*Originals)[Triangle];
			if (Materials->GetValue(Triangle) != Material)
			{
				Materials->SetValue(Triangle, Material);
				bChanged = true;
			}
		}
		if (bChanged) { Component->NotifyMeshUpdated(); }
	}
}

void AChunkedDestructibleConeStructure::ClearMusicPieceMaterials()
{
	RenderMusicEqualizerCells(TConstArrayView<int32>());
	for (const auto& Entry : MusicChunkOriginalMaterials)
	{
		if (ChunkComponents.IsValidIndex(Entry.Key) && ChunkComponents[Entry.Key])
		{
			UDynamicMeshComponent* Component = ChunkComponents[Entry.Key];
			Component->SetNumMaterials(MusicMaterialSlot);
			if (Component->GetMesh()) { Component->GetMesh()->DiscardTriangleGroups(); }
		}
	}
	MusicChunkOriginalMaterials.Empty();
	MusicCellGroupIndices.Empty();
	MusicGlowGroups.Empty();
	MusicSquareRows.Empty();
	MusicHighlightedSquareGroups.Empty();
	MusicChunkShellTriangles.Empty();
	MusicSquareChunkTriangles.Empty();
	if (MusicSquareComponent)
	{
		RemoveInstanceComponent(MusicSquareComponent);
		MusicSquareComponent->DestroyComponent();
		MusicSquareComponent = nullptr;
	}
	CachedMusicGlowGroupSize = 0.0f;
}
