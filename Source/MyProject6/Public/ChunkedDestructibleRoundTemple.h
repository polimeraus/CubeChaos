#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleRoundTemple.generated.h"

/** Simple circular arcades, an open gallery and a conical roof. All geometry uses
 * the project's shared per-cell weapon damage, blast, restore and collision system. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Chunked Destructible Round Temple"))
class MYPROJECT6_API AChunkedDestructibleRoundTemple : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FRoundTempleTest;
public:
    AChunkedDestructibleRoundTemple();
    virtual bool UsesPlanarAnimation() const override { return false; }

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Round Temple")
    void RebuildTemple();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Body", meta=(ClampMin="400",ClampMax="3000",Units="cm"))
    float BodyRadius = 900;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Body", meta=(ClampMin="220",ClampMax="1200",Units="cm"))
    float LowerFloorHeight = 320;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Body", meta=(ClampMin="0.65",ClampMax="1"))
    float UpperRadiusRatio = .94f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Body", meta=(ClampMin="160",ClampMax="1000",Units="cm"))
    float UpperFloorHeight = 250;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Body", meta=(ClampMin="20",ClampMax="100",Units="cm"))
    float WallThickness = 42;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Body", meta=(ClampMin="15",ClampMax="80",Units="cm"))
    float FloorBandHeight = 32;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Arcades", meta=(ClampMin="8",ClampMax="40"))
    int32 LowerArchCount = 20;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Arcades", meta=(ClampMin="8",ClampMax="40"))
    int32 UpperArchCount = 24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Arcades", meta=(ClampMin="0.35",ClampMax="0.8"))
    float ArchWidthRatio = .62f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Arcades", meta=(ClampMin="4",ClampMax="20"))
    int32 ArchSegments = 10;
    /** The central lower opening is a simple rectangular doorway, facing local -Y. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Arcades")
    bool bRectangularEntrance = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Gallery", meta=(ClampMin="80",ClampMax="500",Units="cm"))
    float GalleryHeight = 150;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Gallery", meta=(ClampMin="12",ClampMax="60",Units="cm"))
    float GalleryPostWidth = 22;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Roof", meta=(ClampMin="300",ClampMax="3000",Units="cm"))
    float RoofHeight = 820;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Roof", meta=(ClampMin="20",ClampMax="300",Units="cm"))
    float RoofOverhang = 115;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Roof", meta=(ClampMin="10",ClampMax="100",Units="cm"))
    float RoofThickness = 28;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Roof", meta=(ClampMin="4",ClampMax="40"))
    int32 RoofCourses = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Steps", meta=(ClampMin="40",ClampMax="600",Units="cm"))
    float PlatformHeight = 220;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Steps", meta=(ClampMin="20",ClampMax="200",Units="cm"))
    float PlatformMargin = 65;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Steps")
    bool bFrontSteps = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Steps", meta=(ClampMin="3",ClampMax="30",EditCondition="bFrontSteps"))
    int32 StepCount = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Steps", meta=(ClampMin="25",ClampMax="100",Units="cm",EditCondition="bFrontSteps"))
    float StepDepth = 38;
    /** Partial stair fan, centered on -Y; the back remains a plain round plinth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Steps", meta=(ClampMin="30",ClampMax="240",Units="deg",EditCondition="bFrontSteps"))
    float StairArcDegrees = 150;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Pieces", meta=(ClampMin="32",ClampMax="160"))
    int32 CircleSegments = 96;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Pieces", meta=(ClampMin="50",ClampMax="300",Units="cm"))
    float PieceSize = 110;
    /** Zero keeps the intact building visually clean; cells still disappear separately. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Pieces", meta=(ClampMin="0",ClampMax="3",Units="cm"))
    float PieceGap = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Materials")
    TObjectPtr<UMaterialInterface> StoneMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Materials")
    TObjectPtr<UMaterialInterface> BandMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Materials")
    TObjectPtr<UMaterialInterface> RoofMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round Temple|Materials")
    TObjectPtr<UMaterialInterface> StepMaterial;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Round Temple|Statistics")
    int32 GeneratedPieceCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Round Temple|Statistics")
    int32 GeneratedRoofPieces = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Round Temple|Statistics")
    int32 GeneratedStepPieces = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Round Temple|Statistics", meta=(Units="cm"))
    float GeneratedHeight = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Round Temple|Statistics")
    FString GenerationWarning;

    virtual bool WorldLocationToCell(FVector P, FIntPoint& Cell) const override;
#if WITH_EDITOR
    virtual bool CanEditChange(const FProperty* Property) const override;
#endif
protected:
    virtual bool CalculateLayout() override;
    virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
    virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& Mesh) const override;
    virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& Collision) const override;
    virtual FVector GetCellLocalCenter(FIntPoint Cell) const override;
    virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;
    virtual bool ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& Cell) const override;
    virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
    virtual int32 SelectCellsInRadius(FVector P, float Radius, EChunkedFloorBlastShape Shape, TFunctionRef<void(int32,float)> Function) override;
    virtual void MarkCellChunksDirty(FIntPoint Cell) override;
private:
    struct FPiece
    {
        FVector V[8];
        FBox Bounds;
        int32 Material = 0;
        bool Contains(FVector P, double Tolerance) const;
    };
    TArray<FPiece> Pieces;
    bool bBudgetExceeded = false;
    void AddPiece(const FVector (&Vertices)[8], int32 Material);
    void Wedge(double A, double B, double R0, double R1, double Inner0, double Inner1, double Z0, double Z1, int32 Material);
    void Ring(double Outer, double Inner, double Z0, double Z1, int32 Material, double Arc = UE_DOUBLE_TWO_PI);
    void Facade(double Angle, double Radius, double Left, double Right, double BottomLeft, double BottomRight, double Top, double Thickness, int32 Material);
    void Arcade(double Radius, double Z, double Height, int32 Bays, bool bEntrance);
    bool FindPiece(FVector P, FIntPoint& Cell, double Tolerance) const;
    void AppendPiece(UE::Geometry::FDynamicMesh3& Mesh, const FPiece& Piece, int32 Material) const;
};
