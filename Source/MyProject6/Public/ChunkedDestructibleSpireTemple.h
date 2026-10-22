#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleSpireTemple.generated.h"

/** Stone spire on two circular drums, with an optional pierced rectangular enclosure.
 * Uses the same chunk meshes, per-piece health, weapon hits and explosions as the other structures. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Chunked Destructible Spire Temple"))
class MYPROJECT6_API AChunkedDestructibleSpireTemple : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FSpireTempleTest;
public:
    AChunkedDestructibleSpireTemple();
    virtual bool UsesPlanarAnimation() const override { return false; }

    /** Rebuild after runtime parameter changes; also restores all destroyed pieces. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Spire Temple")
    void RebuildTemple();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Spire")
    bool bSpire = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Spire", meta=(ClampMin="100",ClampMax="4000",Units="cm",EditCondition="bSpire"))
    float SpireRadius = 490;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Spire", meta=(ClampMin="200",ClampMax="12000",Units="cm",EditCondition="bSpire"))
    float SpireHeight = 2350;
    /** Zero makes a sharp point; positive values make a truncated cone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Spire", meta=(ClampMin="0",ClampMax="0.8",EditCondition="bSpire"))
    float SpireTipRatio = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Spire", meta=(ClampMin="8",ClampMax="128",EditCondition="bSpire"))
    int32 SpireSides = 48;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Spire", meta=(ClampMin="3",ClampMax="128",EditCondition="bSpire"))
    int32 SpireCourses = 30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Spire", meta=(ClampMin="5",ClampMax="150",Units="cm",EditCondition="bSpire"))
    float SpireShellThickness = 28;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Spire", meta=(EditCondition="bSpire"))
    bool bStaggerSpireJoints = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Spire", meta=(ClampMin="0",ClampMax="150",Units="cm",EditCondition="bSpire"))
    float SpireCollarHeight = 70;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drums", meta=(ClampMin="200",ClampMax="5000",Units="cm"))
    float LowerDrumRadius = 740;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drums", meta=(ClampMin="150",ClampMax="2000",Units="cm"))
    float LowerDrumHeight = 290;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drums", meta=(ClampMin="150",ClampMax="5000",Units="cm"))
    float UpperDrumRadius = 650;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drums", meta=(ClampMin="150",ClampMax="2500",Units="cm"))
    float UpperDrumHeight = 400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drums", meta=(ClampMin="16",ClampMax="128"))
    int32 DrumSides = 64;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drums", meta=(ClampMin="10",ClampMax="120",Units="cm"))
    float DrumWallThickness = 40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drums", meta=(ClampMin="0",ClampMax="100",Units="cm"))
    float CorniceOverhang = 32;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drums", meta=(ClampMin="5",ClampMax="120",Units="cm"))
    float CorniceHeight = 24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drums", meta=(ClampMin="0",ClampMax="150",Units="cm"))
    float PlinthHeight = 22;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drums")
    bool bInteriorFloors = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drum Openings")
    bool bDrumEntrance = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drum Openings", meta=(ClampMin="80",ClampMax="600",Units="cm",EditCondition="bDrumEntrance"))
    float DrumEntranceWidth = 170;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drum Openings", meta=(ClampMin="100",ClampMax="1500",Units="cm",EditCondition="bDrumEntrance"))
    float DrumEntranceHeight = 240;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drum Openings")
    bool bDrumWindows = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drum Openings", meta=(ClampMin="4",ClampMax="64",EditCondition="bDrumWindows"))
    int32 DrumWindowsAround = 24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drum Openings", meta=(ClampMin="1",ClampMax="6",EditCondition="bDrumWindows"))
    int32 DrumWindowRows = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drum Openings", meta=(ClampMin="0.1",ClampMax="0.8",EditCondition="bDrumWindows"))
    float DrumWindowWidthRatio = .38f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Drum Openings", meta=(ClampMin="0.1",ClampMax="0.85",EditCondition="bDrumWindows"))
    float DrumWindowHeightRatio = .57f;

    /** Disable to remove the entire enclosure, including its gate and foundation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(DisplayName="Rectangular Enclosure"))
    bool bRectangularEnclosure = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(ClampMin="800",ClampMax="16000",Units="cm",EditCondition="bRectangularEnclosure || bCourtyardFloor"))
    float CourtyardWidth = 3600;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(ClampMin="800",ClampMax="16000",Units="cm",EditCondition="bRectangularEnclosure || bCourtyardFloor"))
    float CourtyardDepth = 3000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(ClampMin="200",ClampMax="2000",Units="cm",EditCondition="bRectangularEnclosure"))
    float EnclosureHeight = 720;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(ClampMin="15",ClampMax="180",Units="cm",EditCondition="bRectangularEnclosure"))
    float EnclosureThickness = 65;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(ClampMin="3",ClampMax="60",EditCondition="bRectangularEnclosure"))
    int32 FrontBays = 18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(ClampMin="3",ClampMax="60",EditCondition="bRectangularEnclosure"))
    int32 SideBays = 15;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(EditCondition="bRectangularEnclosure"))
    bool bEnclosureOpenings = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(ClampMin="0.15",ClampMax="0.75",EditCondition="bRectangularEnclosure && bEnclosureOpenings"))
    float EnclosureOpeningRatio = .46f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(ClampMin="0",ClampMax="50",Units="cm",EditCondition="bRectangularEnclosure"))
    float EnclosurePierProjection = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard")
    bool bCourtyardFloor = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Courtyard", meta=(ClampMin="5",ClampMax="100",Units="cm",EditCondition="bCourtyardFloor"))
    float CourtyardFloorThickness = 18;

    /** Front is local -Y. Gate is a real walk-through opening down to ground level. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Gate", meta=(EditCondition="bRectangularEnclosure"))
    bool bGate = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Gate", meta=(ClampMin="100",ClampMax="1600",Units="cm",EditCondition="bRectangularEnclosure && bGate"))
    float GateWidth = 560;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Gate", meta=(ClampMin="150",ClampMax="1800",Units="cm",EditCondition="bRectangularEnclosure && bGate"))
    float GateHeight = 540;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Gate", meta=(ClampMin="10",ClampMax="150",Units="cm",EditCondition="bRectangularEnclosure && bGate"))
    float GateFrameWidth = 60;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Gate", meta=(ClampMin="0",ClampMax="180",Units="cm",EditCondition="bRectangularEnclosure && bGate"))
    float GateProjection = 45;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Gate", meta=(EditCondition="bRectangularEnclosure && bGate"))
    bool bRearGate = false;

    /** Target block size for enclosure walls, drum rows and radial slabs.
     * Circular resolution and spire courses/sides are controlled separately. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Pieces", meta=(ClampMin="40",ClampMax="500",Units="cm"))
    float PieceSize = 110;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Pieces", meta=(ClampMin="0",ClampMax="5",Units="cm"))
    float PieceGap = .65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Materials")
    TObjectPtr<UMaterialInterface> StoneMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Materials")
    TObjectPtr<UMaterialInterface> TrimMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Materials")
    TObjectPtr<UMaterialInterface> SpireMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spire Temple|Materials")
    TObjectPtr<UMaterialInterface> FloorMaterial;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spire Temple|Statistics")
    int32 GeneratedPieceCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spire Temple|Statistics")
    int32 GeneratedEnclosurePieces = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spire Temple|Statistics")
    int32 GeneratedSpirePieces = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spire Temple|Statistics")
    int32 GeneratedOpeningCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spire Temple|Statistics", meta=(Units="cm"))
    float GeneratedHeight = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spire Temple|Statistics")
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
    void Box(FVector Min, FVector Max, int32 Material, int32 Face = -1);
    void Wedge(double A, double B, double R0, double R1, double Inner0, double Inner1, double Z0, double Z1, int32 Material);
    void Ring(double Outer, double Inner, double Z0, double Z1, int32 Material);
    void Drum(double Radius, double Z, double Height, bool bUpper);
    void EnclosureFace(int32 Face);
    bool FindPiece(FVector P, FIntPoint& Cell, double Tolerance) const;
    void AppendPiece(UE::Geometry::FDynamicMesh3& Mesh, const FPiece& Piece, int32 Material) const;
};
