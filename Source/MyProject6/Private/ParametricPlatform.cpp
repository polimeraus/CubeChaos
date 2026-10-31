#include "ParametricPlatform.h"
#include "ParametricGround.h"
#include "ParametricGroundGeometry.h"
#include "ChunkedDestructibleTriangle.h"
#include "ChunkedDestructibleRoundedBlob.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Curve/PolygonOffsetUtils.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "ConstrainedDelaunay2.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

using namespace UE::Geometry;

namespace
{
    double Positive(double Value, double Fallback)
    {
        return FMath::IsFinite(Value) ? FMath::Max(FMath::Abs(Value), 1.0) : Fallback;
    }
    template<typename T> T ReadSetting(const UObject* Source, const TCHAR* Name)
    {
        const FProperty* Property = Source->GetClass()->FindPropertyByName(Name);
        check(Property);
        return *Property->ContainerPtrToValuePtr<T>(Source);
    }

    bool TriangulateSurfaces(const TArray<FGeneralPolygon2d>& Polygons,
        TArray<FChunkedCachedSpiralTriangle>& OutTriangles)
    {
        OutTriangles.Reset();
        for (const FGeneralPolygon2d& Polygon : Polygons)
        {
            FConstrainedDelaunay2d Triangulation;
            Triangulation.bOutputCCW = true;
            Triangulation.Add(Polygon);
            if (!Triangulation.Triangulate()) { OutTriangles.Reset(); return false; }
            for (const FIndex3i& T : Triangulation.Triangles)
            {
                const FVector2D A = Triangulation.Vertices[T.A];
                const FVector2D B = Triangulation.Vertices[T.B];
                const FVector2D C = Triangulation.Vertices[T.C];
                if (FMath::Abs(FVector2D::CrossProduct(B - A, C - A)) <= UE_DOUBLE_SMALL_NUMBER) continue;
                auto& Triangle = OutTriangles.Emplace_GetRef();
                Triangle.A = A; Triangle.B = B; Triangle.C = C;
            }
        }
        return true;
    }
}

AParametricPlatform::AParametricPlatform()
{
    ShapeSize = FVector2D(6000, 4500);
    BorderWidth = 60;
    SecondaryMaterialIndex = 2;
    bUseBrightBorder = false;
    PaletteTints = {FLinearColor(.075f, .09f, .115f), FLinearColor(1.f, .55f, .025f), FLinearColor(.3f, .45f, .6f)};
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ground(
        TEXT("/Game/GameplayActors/Ground/Materials/M_ParametricGround.M_ParametricGround"));
    if (Ground.Succeeded()) { FloorMaterials = {Ground.Object, Ground.Object, Ground.Object}; }
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> PadSurface(
        TEXT("/Game/GameplayActors/Platforms/ParametricPlatform/Materials/M_PlatformCirclesAndPaths.M_PlatformCirclesAndPaths"));
    if (PadSurface.Succeeded()) PadPathSettings.SurfaceMaterial = PadSurface.Object;
}

void AParametricPlatform::GenerateFloor(bool bPreserveCellState)
{
    if (IsTemplate() || bBuildingPlatform) return;
    TGuardValue<bool> Building(bBuildingPlatform, true);
    Thickness = Positive(Thickness, 50);
    TileSize = FVector2D(Positive(TileSize.X, 100), Positive(TileSize.Y, 100));
    ShapeSize = FVector2D(Positive(ShapeSize.X, 6000), Positive(ShapeSize.Y, 4500));
    CellDimensions = FVector(TileSize.X, TileSize.Y, Thickness);
    BoundaryInset = FMath::IsFinite(BoundaryInset) ? FMath::Max(BoundaryInset, 0.f) : 0.f;
    bUseBrightBorder = BorderStyle == EPlatformBorder::CellBorder;
    SyncLegacyFieldsFromMotifSettings();
    SyncLegacyFieldsFromSpiralSettings();
    // Changing storage mode must not copy the old single-surface record into a destructible grid.
    Super::GenerateFloor(bPreserveCellState && bGeneratedPlatform && GeneratedMode == PlatformMode);
    GeneratedMode = PlatformMode;
    bGeneratedPlatform = true;
}

void AParametricPlatform::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    // Chunk meshes are transient. Restore them when a saved level opens, without requiring an edit.
    if (HasAnyFlags(RF_WasLoaded) && !IsTemplate() && !bGeneratedPlatform && FloorRoot)
        GenerateFloor(false);
}

void AParametricPlatform::RebuildPlatform() { GenerateFloor(false); }
void AParametricPlatform::ApplyMaterialPattern()
{
    if (PlatformMode == EPlatformMode::Solid || !bGeneratedPlatform || GeneratedMode != PlatformMode ||
        FloorPattern == EChunkedFloorPattern::CirclesAndPaths || PadPathMaterialInstance)
        GenerateFloor(true);
    else
    {
        SyncLegacyFieldsFromMotifSettings();
        SyncLegacyFieldsFromSpiralSettings();
        Super::ApplyMaterialPattern();
    }
}
void AParametricPlatform::SetCellDimensions(FVector Dimensions, bool bPreserveCellState)
{
    TileSize = FVector2D(Dimensions.X, Dimensions.Y);
    Thickness = Dimensions.Z;
    GenerateFloor(bPreserveCellState);
}
void AParametricPlatform::ConfigureFloor(FVector2D Size, FVector Dimensions, FVector2D Spacing, bool bPreserveCellState)
{
    ShapeSize = Size;
    CellSpacing = Spacing;
    SetCellDimensions(Dimensions, bPreserveCellState);
}
void AParametricPlatform::ConfigureDecorativeFloor(EChunkedDecorativeFloorShape Shape, FVector2D Size,
    FVector Dimensions, FVector2D Spacing, bool bPreserveCellState)
{
    OuterShape = Shape;
    ConfigureFloor(Size, Dimensions, Spacing, bPreserveCellState);
}
void AParametricPlatform::SetPlatformMode(EPlatformMode NewMode)
{
    PlatformMode = NewMode;
    GenerateFloor(false);
}
void AParametricPlatform::ConfigurePlatform(EChunkedDecorativeFloorShape Shape, FVector2D Size,
    float NewThickness, EPlatformMode Mode)
{
    OuterShape = Shape;
    ShapeSize = Size;
    Thickness = NewThickness;
    PlatformMode = Mode;
    GenerateFloor(false);
}

bool AParametricPlatform::CalculateLayout()
{
    // Resolve physical dimensions before the base class allocates either the single slab
    // or its destruction grid. The actor origin remains at the circular hub.
    if (OuterShape == EChunkedDecorativeFloorShape::ParametricShape) PrepareParametricShape();
    if (PlatformMode == EPlatformMode::Cellular)
    {
        const bool Result = Super::CalculateLayout();
        if (Result) UpdatePadPathMaterial();
        return Result;
    }
    // A single surface record lets shared motif/animation code address this slab. It is never
    // damageable, and its count is constant even for kilometre-sized grounds. No tile grid exists.
    TGuardValue<FVector> Dimensions(CellDimensions,
        FVector(ShapeSize.X, (OuterShape == EChunkedDecorativeFloorShape::Circle ||
            OuterShape == EChunkedDecorativeFloorShape::Square) ? ShapeSize.X : ShapeSize.Y, Thickness));
    TGuardValue<FVector2D> Spacing(CellSpacing, FVector2D::ZeroVector);
    TGuardValue<EChunkedFloorFillRule> Fill(FillRule, EChunkedFloorFillRule::StretchCells);
    const bool Result = Super::CalculateLayout();
    // Bound visual tessellation independently of destruction storage.
    const int32 Budget = FMath::Clamp(MaxSolidPatternTiles, 16, 65536);
    double Scale = 1;
    while (FMath::CeilToDouble(ShapeSize.X / (TileSize.X * Scale)) *
        FMath::CeilToDouble(ShapeSize.Y / (TileSize.Y * Scale)) > Budget) Scale *= 2;
    EffectiveSolidTileSize = TileSize * Scale;
    if (Result) UpdatePadPathMaterial();
    return Result;
}

int32 AParametricPlatform::EvaluatePatternMaterial(FIntPoint Cell) const
{
    if (PlatformMode == EPlatformMode::Cellular) return Super::EvaluatePatternMaterial(Cell);
    if (FloorPattern == EChunkedFloorPattern::HeartMotif) return ClampMaterialIndex(HeartMotifSettings.BackgroundMaterialIndex);
    if (FloorPattern == EChunkedFloorPattern::StarMotif) return ClampMaterialIndex(StarMotifSettings.BackgroundMaterialIndex);
    if (FloorPattern == EChunkedFloorPattern::FlowerMotif) return ClampMaterialIndex(FlowerMotifSettings.BackgroundMaterialIndex);
    return ClampMaterialIndex(PrimaryMaterialIndex);
}

bool AParametricPlatform::UsesRegularGroundSlab() const
{
    if (UsesRoundedCorners()) return false;
    return OuterShape == EChunkedDecorativeFloorShape::Square || OuterShape == EChunkedDecorativeFloorShape::Circle ||
        (OuterShape == EChunkedDecorativeFloorShape::Hexagon &&
            FMath::IsNearlyEqual(ShapeSize.X, ShapeSize.Y / FMath::Cos(UE_DOUBLE_PI / 6), .01));
}

void AParametricPlatform::BuildSolidMesh(FDynamicMesh3& Mesh) const
{
    const int32 BaseMaterial = EvaluatePatternMaterial(FIntPoint::ZeroValue);
    if (UsesRegularGroundSlab())
    {
        const auto Shape = OuterShape == EChunkedDecorativeFloorShape::Circle ? EParametricGroundShape::Circle :
            OuterShape == EChunkedDecorativeFloorShape::Square ? EParametricGroundShape::Square : EParametricGroundShape::Hexagon;
        ParametricGroundGeometry::BuildSlab(Mesh, Shape,
            Shape == EParametricGroundShape::Hexagon ? ShapeSize.Y : ShapeSize.X,
            Thickness, FMath::Max(UVWorldUnits, 1.f), CircleEdgeSegments,
            BorderStyle == EPlatformBorder::InsetBand, BoundaryInset, BorderWidth);
        for (int32 Triangle : Mesh.TriangleIndicesItr())
        {
            auto* IDs = Mesh.Attributes()->GetMaterialID();
            IDs->SetValue(Triangle, IDs->GetValue(Triangle) == 1 ? ClampMaterialIndex(BorderMaterialIndex) : BaseMaterial);
        }
    }
    else
    {
        // Use the cached triangulation for both star-shaped outlines and concave block pieces.
        Mesh.EnableAttributes();
        Mesh.Attributes()->EnableMaterialID();
        TArray<int32> Top, Bottom;
        TMap<FVector2D, int32> PointIndices;
        ForEachShapeBoundary([&](const TArray<FVector2D>& Boundary)
        {
        for (const FVector2D& P : Boundary)
        {
            PointIndices.Add(P, Top.Num());
            Top.Add(Mesh.AppendVertex(FVector3d(P.X, P.Y, 0)));
            Bottom.Add(Mesh.AppendVertex(FVector3d(P.X, P.Y, -Thickness)));
        }
        });
        auto PointIndex = [&](const FVector2D& P)
        {
            if (const int32* Existing = PointIndices.Find(P)) return *Existing;
            const int32 Index = Top.Num();
            PointIndices.Add(P, Index);
            Top.Add(Mesh.AppendVertex(FVector3d(P.X, P.Y, 0)));
            Bottom.Add(Mesh.AppendVertex(FVector3d(P.X, P.Y, -Thickness)));
            return Index;
        };
        auto Triangle = [&](int32 A, int32 B, int32 C, FVector3f Normal, bool Side)
        {
            const FIndex3i V(A, C, B);
            const int32 ID = Mesh.AppendTriangle(V);
            if (ID < 0) return;
            Mesh.Attributes()->GetMaterialID()->SetValue(ID, BaseMaterial);
            FIndex3i N, UV;
            for (int32 I = 0; I < 3; ++I)
            {
                const FVector3d P = Mesh.GetVertex(V[I]);
                N[I] = Mesh.Attributes()->PrimaryNormals()->AppendElement(Normal);
                const FVector2f Coord = Side ? FVector2f(-P.X * Normal.Y + P.Y * Normal.X, P.Z) : FVector2f(P.X, P.Y);
                UV[I] = Mesh.Attributes()->PrimaryUV()->AppendElement(Coord / FMath::Max(UVWorldUnits, 1.f));
            }
            Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID, N);
            Mesh.Attributes()->PrimaryUV()->SetTriangle(ID, UV);
        };
        for (const auto& T : CachedShapeTriangles)
        {
            const int32 A = PointIndex(T.A), B = PointIndex(T.B), C = PointIndex(T.C);
            Triangle(Top[A], Top[B], Top[C], FVector3f::UnitZ(), false);
            Triangle(Bottom[A], Bottom[C], Bottom[B], -FVector3f::UnitZ(), false);
        }
        ForEachShapeBoundary([&](const TArray<FVector2D>& Boundary)
        {
        for (int32 I = 0; I < Boundary.Num(); ++I)
        {
            const int32 A = PointIndex(Boundary[I]);
            const int32 B = PointIndex(Boundary[(I + 1) % Boundary.Num()]);
            const FVector2D Edge = Boundary[(I + 1) % Boundary.Num()] - Boundary[I];
            const FVector3f Normal = FVector3f(Edge.Y, -Edge.X, 0).GetSafeNormal();
            Triangle(Top[A], Bottom[A], Bottom[B], Normal, true);
            Triangle(Top[A], Bottom[B], Top[B], Normal, true);
        }
        });
    }
    // Keep the optional bottom and side-neon controls in both generation modes.
    TArray<int32> Remove;
    for (int32 ID : Mesh.TriangleIndicesItr())
    {
        const FIndex3i V = Mesh.GetTriangle(ID);
        const double A = Mesh.GetVertex(V.A).Z, B = Mesh.GetVertex(V.B).Z, C = Mesh.GetVertex(V.C).Z;
        if (!bGenerateBottomFaces && A < 0 && B < 0 && C < 0) Remove.Add(ID);
        const bool Side = FMath::Min3(A, B, C) < 0 && FMath::Max3(A, B, C) >= 0;
        if (Side && SideNeonMaterialInstance)
        {
            Mesh.Attributes()->GetMaterialID()->SetValue(ID, FMath::Max(FloorMaterials.Num(), 1));
            FIndex3i UV;
            for (int32 I = 0; I < 3; ++I)
                UV[I] = Mesh.Attributes()->PrimaryUV()->AppendElement(FVector2f((Mesh.GetVertex(V[I]).Z + Thickness) / Thickness, 0));
            Mesh.Attributes()->PrimaryUV()->SetTriangle(ID, UV);
        }
    }
    for (int32 ID : Remove) Mesh.RemoveTriangle(ID);
    AppendSolidTiles(Mesh);
    AppendDecorativeSurfacePatterns(0, Mesh);
}

void AParametricPlatform::AppendSolidTiles(FDynamicMesh3& Mesh) const
{
    const bool Tiled = FloorPattern >= EChunkedFloorPattern::Checkerboard && FloorPattern <= EChunkedFloorPattern::SeededRandom;
    if (!Tiled && BorderStyle != EPlatformBorder::CellBorder) return;
    const int32 NX = FMath::Max(1, FMath::CeilToInt(ShapeSize.X / EffectiveSolidTileSize.X));
    const int32 NY = FMath::Max(1, FMath::CeilToInt(ShapeSize.Y / EffectiveSolidTileSize.Y));
    const FVector2D Min = -FVector2D(NX * EffectiveSolidTileSize.X, NY * EffectiveSolidTileSize.Y) * .5;
    const int32 Scale = FMath::Max(PatternCellScale, 1);
    const int32 BaseMaterial = EvaluatePatternMaterial(FIntPoint::ZeroValue);
    for (int32 Y = 0; Y < NY; ++Y) for (int32 X = 0; X < NX; ++X)
    {
        const FVector2D A = Min + FVector2D(X * EffectiveSolidTileSize.X, Y * EffectiveSolidTileSize.Y);
        const FVector2D B = A + EffectiveSolidTileSize;
        bool Secondary = false;
        switch (FloorPattern)
        {
        case EChunkedFloorPattern::Checkerboard: Secondary = ((X / Scale + Y / Scale) & 1) != 0; break;
        case EChunkedFloorPattern::StripesX: Secondary = ((X / Scale) & 1) != 0; break;
        case EChunkedFloorPattern::StripesY: Secondary = ((Y / Scale) & 1) != 0; break;
        case EChunkedFloorPattern::DiagonalStripes: Secondary = (((X + Y) / Scale) & 1) != 0; break;
        case EChunkedFloorPattern::ConcentricRings:
            Secondary = (FMath::FloorToInt(FVector2D::Distance(FVector2D(X, Y), FVector2D(NX - 1, NY - 1) * .5) / Scale) & 1) != 0; break;
        case EChunkedFloorPattern::SeededRandom:
            Secondary = (HashCombineFast(HashCombineFast(GetTypeHash(X / Scale), GetTypeHash(Y / Scale)), GetTypeHash(PatternRandomSeed)) & 1u) != 0; break;
        default: break;
        }
        int32 Material = Secondary ? ClampMaterialIndex(SecondaryMaterialIndex) : BaseMaterial;
        if (BorderStyle == EPlatformBorder::CellBorder && !IsPointInsideOuterShape((A + B) * .5, BorderWidth))
            Material = ClampMaterialIndex(BorderMaterialIndex);
        if (Material == BaseMaterial) continue;
        AppendPolygonSurface(Mesh, 0, {A, FVector2D(B.X, A.Y), B, FVector2D(A.X, B.Y)}, Material, .05);
    }
}

void AParametricPlatform::AppendInsetBorder(int32 ChunkIndex, FDynamicMesh3& Mesh) const
{
    if (BorderStyle != EPlatformBorder::InsetBand || BorderWidth <= 0 || CachedShapeOutline.Num() < 3) return;
    if (PlatformMode == EPlatformMode::Solid && UsesRegularGroundSlab() &&
        (FloorPattern == EChunkedFloorPattern::Solid || FloorPattern == EChunkedFloorPattern::CirclesAndPaths)) return;
    const TArray<FGeneralPolygon2d> Shape = {GetPlatformPolygon()};
    TArray<FGeneralPolygon2d> Outside, Inside, Band;
    if (BoundaryInset <= UE_SMALL_NUMBER) Outside = Shape;
    else PolygonsOffset(-BoundaryInset, Shape, Outside, false, 4, EPolygonOffsetJoinType::Miter);
    PolygonsOffset(-(BoundaryInset + BorderWidth), Shape, Inside, false, 4, EPolygonOffsetJoinType::Miter);
    if (Inside.IsEmpty()) Band = Outside;
    else PolygonsDifference(Outside, Inside, Band);
    for (const FGeneralPolygon2d& Polygon : Band)
    {
        FConstrainedDelaunay2d Triangulation;
        Triangulation.bOutputCCW = true;
        Triangulation.Add(Polygon);
        if (!Triangulation.Triangulate()) continue;
        for (const FIndex3i& T : Triangulation.Triangles)
        {
            if (PlatformMode == EPlatformMode::Solid)
                AppendSolidSurfaceTriangle(Mesh, Triangulation.Vertices[T.A], Triangulation.Vertices[T.B],
                    Triangulation.Vertices[T.C], ClampMaterialIndex(BorderMaterialIndex), .6);
            else
                AppendPolygonSurface(Mesh, ChunkIndex,
                    {Triangulation.Vertices[T.A], Triangulation.Vertices[T.B], Triangulation.Vertices[T.C]},
                    ClampMaterialIndex(BorderMaterialIndex), .6);
        }
    }
}

void AParametricPlatform::AppendSolidSurfaceTriangle(FDynamicMesh3& Mesh,
    const FVector2D& A, const FVector2D& B, const FVector2D& C, int32 MaterialIndex, double SurfaceZ) const
{
    const double UVScale = FMath::Max(UVWorldUnits, 1.f);
    AppendMeshTriangle(Mesh, FVector3d(A.X, A.Y, SurfaceZ), FVector3d(B.X, B.Y, SurfaceZ),
        FVector3d(C.X, C.Y, SurfaceZ), FVector3f::UnitZ(), MaterialIndex,
        FVector2f(A / UVScale), FVector2f(B / UVScale), FVector2f(C / UVScale));
}

void AParametricPlatform::AppendPolygonSurface(FDynamicMesh3& Mesh, int32 ChunkIndex,
    const TArray<FVector2D>& Outline, int32 MaterialIndex, double SurfaceZ) const
{
    if (PlatformMode != EPlatformMode::Solid)
    {
        Super::AppendPolygonSurface(Mesh, ChunkIndex, Outline, MaterialIndex, SurfaceZ);
        return;
    }
    if (Outline.Num() < 3 || CachedShapeOutline.Num() < 3 || Cells.IsEmpty()) return;
    FPatternSurfaceCache& Cache = FindOrAddPatternSurfaceCache(Outline, MaterialIndex, SurfaceZ);
    if (!Cache.CellBuilt[0])
    {
        // Clip the complete motif once. The slab's internal fan edges have no visual meaning.
        const TArray<FGeneralPolygon2d> Motif = {FGeneralPolygon2d(FPolygon2d(Outline))};
        const TArray<FGeneralPolygon2d> Shape = {GetPlatformPolygon()};
        TArray<FGeneralPolygon2d> Clipped;
        TArray<FChunkedCachedSpiralTriangle> Triangles;
        if (!PolygonsIntersection(Motif, Shape, Clipped) || !TriangulateSurfaces(Clipped, Triangles))
        {
            Super::AppendPolygonSurface(Mesh, ChunkIndex, Outline, MaterialIndex, SurfaceZ);
            return;
        }
        auto& Cached = Cache.CellTriangles[0];
        Cached.Reset(Triangles.Num());
        for (const auto& T : Triangles)
        {
            auto& Triangle = Cached.Emplace_GetRef();
            Triangle.A = T.A; Triangle.B = T.B; Triangle.C = T.C;
        }
        Cache.CellBuilt[0] = true;
    }
    for (const auto& T : Cache.CellTriangles[0])
        AppendSolidSurfaceTriangle(Mesh, T.A, T.B, T.C, MaterialIndex, SurfaceZ);
}

void AParametricPlatform::ClipSpiralSurface(const TArray<TArray<FVector2D>>& CellPolygons,
    TArray<FChunkedCachedSpiralTriangle>& OutTriangles) const
{
    if (PlatformMode != EPlatformMode::Solid)
    {
        Super::ClipSpiralSurface(CellPolygons, OutTriangles);
        return;
    }
    // Non-zero polygon fill combines the sampled strip and its round center cap, removing
    // shared internal edges and overlap without reducing the curve's sampling resolution.
    TArray<FGeneralPolygon2d> Strip;
    Strip.Reserve(CachedSpiralSourceTriangles.Num());
    for (const auto& T : CachedSpiralSourceTriangles)
        Strip.Emplace(FPolygon2d(TArray<FVector2D>{T.A, T.B, T.C}));
    const TArray<FGeneralPolygon2d> Shape = {GetPlatformPolygon()};
    TArray<FGeneralPolygon2d> Clipped;
    if (!PolygonsIntersection(Strip, Shape, Clipped) || !TriangulateSurfaces(Clipped, OutTriangles))
        Super::ClipSpiralSurface(CellPolygons, OutTriangles);
}

void AParametricPlatform::BuildChunkMesh(int32 ChunkIndex, FDynamicMesh3& Mesh) const
{
    if (PlatformMode == EPlatformMode::Solid) BuildSolidMesh(Mesh);
    else Super::BuildChunkMesh(ChunkIndex, Mesh);
    AppendInsetBorder(ChunkIndex, Mesh);
    ApplyPadPathSurface(Mesh);
}

void AParametricPlatform::BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& Collision) const
{
    if (PlatformMode == EPlatformMode::Cellular) Super::BuildChunkSimpleCollision(ChunkIndex, Collision);
}

int32 AParametricPlatform::SelectCellsInRadius(FVector Location, float Radius, EChunkedFloorBlastShape Shape,
    TFunctionRef<void(int32, float)> CellFunction)
{
    return PlatformMode == EPlatformMode::Cellular ? Super::SelectCellsInRadius(Location, Radius, Shape, CellFunction) : 0;
}

void AParametricPlatform::UpdateTintMaterials()
{
    TintedMaterials.SetNum(FloorMaterials.Num());
    for (int32 I = 0; I < FloorMaterials.Num(); ++I)
    {
        if (!bTintPalette || !FloorMaterials[I]) { TintedMaterials[I] = nullptr; continue; }
        if (!TintedMaterials[I] || TintedMaterials[I]->Parent != FloorMaterials[I])
        {
            TintedMaterials[I] = UMaterialInstanceDynamic::Create(FloorMaterials[I], this);
            TintedMaterials[I]->SetFlags(RF_Transient);
        }
        const FLinearColor Tint = PaletteTints.IsValidIndex(I) ? PaletteTints[I] : FLinearColor::White;
        for (FName Name : {FName(TEXT("Color")), FName(TEXT("BaseColor")), FName(TEXT("Tint"))})
            TintedMaterials[I]->SetVectorParameterValue(Name, Tint);
    }
}

void AParametricPlatform::ConfigureChunkComponent(UDynamicMeshComponent* Component) const
{
    Super::ConfigureChunkComponent(Component);
    if (!Component) return;
    const_cast<AParametricPlatform*>(this)->UpdateTintMaterials();
    for (int32 I = 0; I < TintedMaterials.Num(); ++I)
        if (TintedMaterials[I]) Component->SetMaterial(I, TintedMaterials[I]);
    if (FloorPattern == EChunkedFloorPattern::CirclesAndPaths && PadPathMaterialInstance)
    {
        Component->SetNumMaterials(PadPathMaterialIndex() + 1);
        Component->SetMaterial(PadPathMaterialIndex(), PadPathMaterialInstance);
    }
    if (PlatformMode == EPlatformMode::Solid)
    {
        Component->SetCanEverAffectNavigation(bGenerateCollision);
        Component->SetComplexAsSimpleCollisionEnabled(bGenerateCollision, false);
    }
}

void AParametricPlatform::RebuildBlobCache()
{
    if (BlobProfile == EPlatformBlobProfile::Decorative) { Super::RebuildBlobCache(); return; }
    CachedBlobRadii.Reset();
    FRandomStream Random(BlobShapeSeed);
    for (int32 I = 0; I < FMath::Clamp(BlobLobeCount, 3, 16) * 2; ++I)
    {
        const float Variation = Random.FRand();
        CachedBlobRadii.Add((I % 2) == 0 ? 1 + BlobLobeStrength * (.55f + .45f * Variation) :
            1 - BlobLobeStrength * (.25f + .40f * Variation));
    }
}

float AParametricPlatform::SampleBlobRadius(float Angle) const
{
    if (BlobProfile == EPlatformBlobProfile::Decorative) return Super::SampleBlobRadius(Angle);
    if (CachedBlobRadii.IsEmpty()) return 1;
    const double Position = FMath::Fmod(double(Angle) + UE_DOUBLE_TWO_PI, UE_DOUBLE_TWO_PI) / UE_DOUBLE_TWO_PI * CachedBlobRadii.Num();
    const int32 Index = FMath::FloorToInt(Position);
    const double A = Position - Index;
    auto R = [&](int32 I) { return double(CachedBlobRadii[(I + CachedBlobRadii.Num()) % CachedBlobRadii.Num()]); };
    const double P0 = R(Index - 1), P1 = R(Index), P2 = R(Index + 1), P3 = R(Index + 2);
    const double Spline = .5 * (2 * P1 + (-P0 + P2) * A + (2 * P0 - 5 * P1 + 4 * P2 - P3) * A * A +
        (-P0 + 3 * P1 - 3 * P2 + P3) * A * A * A);
    return FMath::Clamp(FMath::Lerp(FMath::Lerp(P1, P2, A), Spline, double(BlobEdgeRoundness)), .45, 1.55);
}

void AParametricPlatform::ApplyPreset()
{
    AActor* Source = nullptr;
    switch (Preset)
    {
    case EPlatformPreset::Ground: Source = GetMutableDefault<AParametricGround>(); break;
    case EPlatformPreset::Triangle: Source = GetMutableDefault<AChunkedDestructibleTriangle>(); break;
    case EPlatformPreset::RoundedBlob: Source = GetMutableDefault<AChunkedDestructibleRoundedBlob>(); break;
    case EPlatformPreset::DecorativeFloor: Source = GetMutableDefault<AChunkedDestructibleDecorativeFloor>(); break;
    }
    CopyPlatformSettingsFrom(Source);
}

bool AParametricPlatform::CopyPlatformSettingsFrom(AActor* Source)
{
    if (!Source || Source == this) return false;
    if (const AParametricGround* Ground = Cast<AParametricGround>(Source))
    {
        PlatformMode = EPlatformMode::Solid;
        CornerRadius = 0;
        OuterShape = Ground->Shape == EParametricGroundShape::Circle ? EChunkedDecorativeFloorShape::Circle :
            Ground->Shape == EParametricGroundShape::Square ? EChunkedDecorativeFloorShape::Square : EChunkedDecorativeFloorShape::Hexagon;
        ShapeSize = FVector2D(Ground->Size, Ground->Size);
        if (Ground->Shape == EParametricGroundShape::Hexagon) ShapeSize.X /= FMath::Cos(UE_DOUBLE_PI / 6);
        Thickness = Ground->Thickness;
        CircleEdgeSegments = Ground->CircleSegments;
        BorderStyle = Ground->bShowBoundary ? EPlatformBorder::InsetBand : EPlatformBorder::None;
        BoundaryInset = Ground->BoundaryInset;
        BorderWidth = Ground->BoundaryWidth;
        BorderMaterialIndex = 1;
        PrimaryMaterialIndex = 0;
        SecondaryMaterialIndex = 2;
        FloorMaterials = {Ground->GroundMaterial, Ground->BoundaryMaterial, Ground->GroundMaterial};
        PaletteTints = {Ground->GroundColor, Ground->BoundaryColor, Ground->GroundColor};
        bTintPalette = true;
        FloorPattern = EChunkedFloorPattern::Solid;
        UVWorldUnits = Ground->UVTileSize;
        bGenerateCollision = Ground->bEnableCollision;
        bGenerateBottomFaces = true;
        SideNeon.Mode = EFloorSideNeonMode::Off;
        PlanarAnimationType = EPlanarAnimationType::None;
        bReactToMusic = false;
    }
    else if (Source->IsA<AChunkedDestructibleFloor>())
    {
        // Deliberately restrict copying to authoring properties declared on the two floor bases.
        // AActor state, components, delegates, labels and arbitrary level references never enter defaults.
        for (TFieldIterator<FProperty> It(AChunkedDestructibleDecorativeFloor::StaticClass()); It; ++It)
        {
            FProperty* P = *It;
            if (P->GetOwnerClass() != AChunkedDestructibleFloor::StaticClass() &&
                P->GetOwnerClass() != AChunkedDestructibleDecorativeFloor::StaticClass()) continue;
            if (!P->HasAnyPropertyFlags(CPF_Edit) || P->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient)) continue;
            const FProperty* From = Source->GetClass()->FindPropertyByName(P->GetFName());
            if (From && P->SameType(From)) P->CopyCompleteValue(P->ContainerPtrToValuePtr<void>(this), From->ContainerPtrToValuePtr<void>(Source));
        }
        TileSize = FVector2D(CellDimensions.X, CellDimensions.Y);
        Thickness = CellDimensions.Z;
        PlatformMode = EPlatformMode::Cellular;
        bTintPalette = false;
        BorderStyle = Source->IsA<AChunkedDestructibleDecorativeFloor>() && bUseBrightBorder ? EPlatformBorder::CellBorder : EPlatformBorder::None;
        BlobProfile = EPlatformBlobProfile::Decorative;
        if (Source->IsA<AChunkedDestructibleTriangle>())
        {
            OuterShape = EChunkedDecorativeFloorShape::Triangle;
            ShapeSize = FVector2D(ReadSetting<float>(Source, TEXT("BaseLength")), ReadSetting<float>(Source, TEXT("TriangleHeight")));
            TriangleApexOffset = ReadSetting<float>(Source, TEXT("ApexOffset"));
        }
        else if (Source->IsA<AChunkedDestructibleRoundedBlob>())
        {
            OuterShape = EChunkedDecorativeFloorShape::RoundedBlob;
            BlobProfile = EPlatformBlobProfile::Organic;
            ShapeSize = ReadSetting<FVector2D>(Source, TEXT("BlobSize"));
            BlobLobeCount = ReadSetting<int32>(Source, TEXT("LobeCount"));
            BlobLobeStrength = ReadSetting<float>(Source, TEXT("LobeStrength"));
            BlobEdgeRoundness = ReadSetting<float>(Source, TEXT("EdgeRoundness"));
            BlobShapeSeed = ReadSetting<int32>(Source, TEXT("ShapeSeed"));
            BlobOutlineSamplesPerLobe = ReadSetting<int32>(Source, TEXT("OutlineSamplesPerLobe"));
        }
        else if (!Source->IsA<AChunkedDestructibleDecorativeFloor>())
        {
            OuterShape = EChunkedDecorativeFloorShape::Grid;
            ShapeSize = AreaSize;
        }
        if (const AParametricPlatform* Platform = Cast<AParametricPlatform>(Source))
        {
            PlatformMode = Platform->PlatformMode; TileSize = Platform->TileSize; Thickness = Platform->Thickness;
            BorderStyle = Platform->BorderStyle; BoundaryInset = Platform->BoundaryInset; BlobProfile = Platform->BlobProfile;
            bTintPalette = Platform->bTintPalette; PaletteTints = Platform->PaletteTints; MaxSolidPatternTiles = Platform->MaxSolidPatternTiles;
            PadPathSettings = Platform->PadPathSettings;
            CornerRadius = Platform->CornerRadius; CornerSegments = Platform->CornerSegments; BlockShape = Platform->BlockShape;
            ReferenceShape = Platform->ReferenceShape;
            ParametricShapeSettings = Platform->ParametricShapeSettings;
        }
        else CornerRadius = 0;
    }
    else return false;
    GenerateFloor(false);
    return true;
}

#if WITH_EDITOR
bool AParametricPlatform::CanEditChange(const FProperty* Property) const
{
    if (Property)
    {
        const FName Name = Property->GetFName();
        if (Name == TEXT("ShapeSize") && OuterShape == EChunkedDecorativeFloorShape::ParametricShape) return false;
        if (Name == TEXT("AreaSize") || Name == TEXT("CellDimensions") || Name == TEXT("bUseBrightBorder")) return false;
        if (PlatformMode == EPlatformMode::Solid && (Name == TEXT("CellSpacing") || Name == TEXT("FillRule") ||
            Name == TEXT("ChunkSideLength") || Name == TEXT("MaxGeneratedCells") ||
            Property->GetMetaData(TEXT("Category")).StartsWith(TEXT("Chunked Floor|Damage")))) return false;
    }
    return Super::CanEditChange(Property);
}
#endif
