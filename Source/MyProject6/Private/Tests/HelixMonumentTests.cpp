#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ChunkedDestructibleHelixMonument.h"
#include "Chaos/Convex.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/BodySetup.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelixMonumentTest, "MyProject6.Structures.HelixMonument.GeometryAndDestruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHelixMonumentTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Tower = World->SpawnActor<AChunkedDestructibleHelixMonument>();
    if (!TestNotNull(TEXT("Native monument spawns"), Tower)) return false;
    Tower->FlashDuration = 0;
    Tower->bPlayDestroyTickSound = false;
    Tower->bUseDestructionRipple = false;
    auto Triangles = [&]()
    {
        int32 Total = 0;
        for (const auto& C : Tower->ChunkComponents) Total += C->GetDynamicMesh()->GetTriangleCount();
        return Total;
    };
    auto Trace = [&](FVector From, FVector To) { return World->LineTraceTestByChannel(From, To, ECC_Visibility); };

    const int32 Count = Tower->GeneratedFragments, OriginalTris = Triangles();
    TestTrue(TEXT("Reference preset within budget"), Count > 1500 && Count < 10000 && Tower->GenerationWarning.IsEmpty());
    TestEqual(TEXT("Default needle height"), Tower->GeneratedHeight, Tower->Height);
    TestEqual(TEXT("Collision is batched into chunks"), Tower->ChunkComponents.Num(), FMath::DivideAndRoundUp(Count,48));
    int32 Cooked = 0;
    for (const auto& C : Tower->ChunkComponents)
        for (const auto& Hull : C->GetBodySetup()->AggGeom.ConvexElems)
        {
            if (!TestTrue(TEXT("Every stone cooks to a valid collision hull"),
                Hull.GetChaosConvexMesh() && Hull.GetChaosConvexMesh()->IsValidGeometry())) return false;
            ++Cooked;
        }
    TestEqual(TEXT("One convex hull per breakable fragment"), Cooked, Count);
    for (const auto& Stone : Tower->Stones)
        if (!TestTrue(TEXT("Convex damage lookup includes stone centre"), Stone.Contains(Stone.Center,.001))) return false;

    const double Shoulder = Tower->Height*(1.-Tower->CrownHeightRatio);
    auto CrownTrace = [&](double T, double Angle)
    {
        const double Radius = Tower->BaseRadius*Tower->NeckRadiusRatio*FMath::Pow(1.-T,Tower->CrownTaper);
        const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const FVector AtHeight(0,0,FMath::Lerp(Shoulder,double(Tower->Height),T));
        return Trace(AtHeight+Direction*(Radius+25.), AtHeight+Direction*(Radius*.55));
    };
    const double Front = FMath::DegreesToRadians(double(Tower->EntranceAngle));
    TestFalse(TEXT("Lower arch has clear line of sight"), CrownTrace(.16,Front));
    TestTrue(TEXT("Arch pier has collision"), CrownTrace(.16,Front+UE_DOUBLE_PI/Tower->LanternArches));
    TestFalse(TEXT("Upper lancet is pierced"), CrownTrace(.43,Front));
    TestTrue(TEXT("Stone between lancets has collision"), CrownTrace(.43,Front+UE_DOUBLE_PI/Tower->LancetCount));
    for (int32 Step=0; Step<Tower->StairCount; ++Step)
    {
        const double R = Tower->BaseRadius*.94+(Step+.5)*Tower->StairRun/Tower->StairCount;
        const double Z = Tower->PlinthHeight*(1.-double(Step)/Tower->StairCount);
        if (!TestTrue(TEXT("Stair treads rise from ground to the entrance"), Trace({0,-R,Z+5},{0,-R,Z-5}))) return false;
    }

    // Cross only the inset facade, keeping the trace away from an end joint and the adjacent deck.
    const auto* Panel=Tower->Stones.FindByPredicate([&](const auto& S){return S.Slot==2 && S.Center.Z>Shoulder*.42;});
    if (!TestNotNull(TEXT("Ribbon has smoked panels"),Panel)) return false;
    const FVector End=Panel->Center, Start=End+End.GetSafeNormal2D()*65.;
    FHitResult Hit;
    if (!TestTrue(TEXT("Weapon trace hits the dark panel"), World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility))) return false;
    FIntPoint Cell;
    if (!TestTrue(TEXT("Weapon hit resolves a fragment"), Tower->ResolveExistingCellFromHit(Hit,Cell))) return false;
    const int32 Index=Cell.X;
    TestEqual(TEXT("The hit panel uses smoked material"), Tower->EvaluatePatternMaterial(Cell), 2);
    TestTrue(TEXT("Nonlethal damage accepted"), Tower->DamageCellFromHit(Hit,5));
    FChunkedFloorCellInfo Info;
    Tower->GetCellInfo(Cell,Info);
    TestEqual(TEXT("Nonlethal hit consumes fragment health"),Info.Health,20.f);
    TestEqual(TEXT("Nonlethal hit keeps the mesh"),Triangles(),OriginalTris);
    TestTrue(TEXT("Lethal damage accepted"),Tower->DamageCellFromHit(Hit,100));
    Tower->ProcessQueuedDestructions(1.e10);
    Tower->FlushDirtyChunks();
    TestTrue(TEXT("The destroyed panel loses its mesh"),Triangles()<OriginalTris);
    TestFalse(TEXT("The destroyed panel loses its collision"),Trace(Start,End));
    Tower->RestoreCell(Cell);
    TestTrue(TEXT("Restored panel blocks the trace again"),Trace(Start,End));
    TestEqual(TEXT("Restoration recovers original triangle count"),Triangles(),OriginalTris);

    const FTransform Moved(FRotator(0,38,0),FVector(210,-450,100),FVector(1.32,.73,1.12));
    Tower->SetActorTransform(Moved);
    const FVector Point=Moved.TransformPosition(Tower->Stones[Index].Center);
    TestTrue(TEXT("Rotated and scaled instance resolves hit position"),Tower->WorldLocationToCell(Point,Cell));
    TestEqual(TEXT("Transformed point resolves the original fragment"),Cell.X,Index);
    const int32 LocalBlast=Tower->DestroyCellsInRadius(Point,235,EChunkedFloorBlastShape::Sphere);
    Tower->ProcessQueuedDestructions(1.e10);
    Tower->FlushDirtyChunks();
    TestTrue(TEXT("Local blast removes only part of the structure"),LocalBlast>3 && LocalBlast<Count && Triangles()<OriginalTris);
    Tower->SetActorTransform(FTransform::Identity);
    Tower->RebuildMonument();
    TestEqual(TEXT("Rebuild restores the whole structure"),Triangles(),OriginalTris);
    TestEqual(TEXT("Large blast reaches every fragment"),
        Tower->DestroyCellsInRadius(FVector(0,0,1000),10000,EChunkedFloorBlastShape::Sphere),Count);
    Tower->ProcessQueuedDestructions(1.e10);
    Tower->FlushDirtyChunks();
    TestEqual(TEXT("Entire monument can be destroyed"),Triangles(),0);
    TestFalse(TEXT("Empty monument has no pier collision"),CrownTrace(.16,Front+UE_DOUBLE_PI/Tower->LanternArches));

    Tower->LancetCount=0;
    Tower->RebuildMonument();
    TestTrue(TEXT("Closing the lancets adds stone and collision"),CrownTrace(.43,Front));
    Tower->bEntranceStairs=false;
    Tower->bCentralColumn=false;
    Tower->bCrown=false;
    Tower->Turns=2.5f;
    Tower->bClockwise=true;
    Tower->RebuildMonument();
    const int32 VariantCount=Tower->GeneratedFragments, VariantTris=Triangles();
    TestTrue(TEXT("Mirrored open-top variant rebuilds"),VariantTris>0 && VariantCount<Count && Tower->GenerationWarning.IsEmpty());
    TestTrue(TEXT("Removing the crown lowers the silhouette"),Tower->GeneratedHeight<Tower->Height*.8);
    TestFalse(TEXT("Disabling stairs removes their collision"),Trace({0,-1200,300},{0,-1200,1}));
    TestFalse(TEXT("Disabling central column clears the axis"),Trace({0,0,890},{0,0,730}));
    Tower->LowerPierCount=0;
    Tower->RebuildMonument();
    TestTrue(TEXT("Lower piers can be removed independently"),Tower->GeneratedFragments<VariantCount);
    const int32 WithoutPiers=Tower->GeneratedFragments, WithoutPierTris=Triangles();
    AddExpectedMessage(TEXT("Fragment budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    Tower->MaxGeneratedCells=10;
    Tower->RebuildMonument();
    TestEqual(TEXT("Rejected parameter set keeps visible geometry"),Triangles(),WithoutPierTris);
    TestEqual(TEXT("Rejected parameter set keeps fragments"),Tower->GeneratedFragments,WithoutPiers);
    AddInfo(FString::Printf(TEXT("Reference preset contains %d fragments and %d triangles; local blast removed %d fragments."),Count,OriginalTris,LocalBlast));
    return true;
}
#endif
