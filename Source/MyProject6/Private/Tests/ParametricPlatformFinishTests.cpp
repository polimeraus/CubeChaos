#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricPlatform.h"
#include "ParametricLobbyPlatformDestructable.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DamageEvents.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PropertyEditorModule.h"
#include "IPropertyRowGenerator.h"
#include "IDetailTreeNode.h"
#include "PropertyHandle.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricPlatformFinishTest, "MyProject6.Platform.FinishCollisionPadsAndLobbyImport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricPlatformFinishTest::RunTest(const FString& Parameters)
{
    using namespace UE::Geometry;
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* P=World->SpawnActor<AParametricPlatform>();
    P->bUseDestructionRipple=false; P->FlashDuration=0; P->bPlayDestroyTickSound=false;
    P->BorderStyle=EPlatformBorder::None; P->TileSize=FVector2D(125,125);
    P->CornerRadius=100; P->bRoundCorners=false; P->BevelSize=10; P->bBevelEdges=false;
    P->ConfigurePlatform(EChunkedDecorativeFloorShape::Grid,{1000,800},80,EPlatformMode::Solid);
    TestEqual(TEXT("Stored radius does not round with horizontal toggle off"),P->CachedShapeOutline.Num(),4);
    TestEqual(TEXT("Stored bevel is inactive with vertical toggle off"),P->EffectiveBevelSize,0.f);
    P->bRoundCorners=true; P->RebuildPlatform();
    TestTrue(TEXT("Horizontal toggle activates existing radius"),P->CachedShapeOutline.Num()>4);
    TestEqual(TEXT("Horizontal rounding does not enable vertical bevel"),P->EffectiveBevelSize,0.f);
    P->bRoundCorners=false; P->bBevelEdges=true;
    auto Trace = [&](FVector2D XY, FHitResult& Hit)
    {
        return World->LineTraceSingleByChannel(Hit,FVector(XY.X,XY.Y,200),FVector(XY.X,XY.Y,-200),ECC_Pawn) && Hit.GetActor()==P;
    };
    for (auto Mode:{EPlatformMode::Solid,EPlatformMode::Cellular})
    {
        P->ConfigurePlatform(EChunkedDecorativeFloorShape::Grid,{1000,800},80,Mode);
        TestEqual(TEXT("Bevel is clamped to ten percent of thickness"),P->EffectiveBevelSize,8.f);
        FHitResult Hit;
        TestTrue(TEXT("Beveled surface has collision"),Trace({496,13},Hit));
        TestTrue(TEXT("Collision follows the actual bevel plane"),FMath::Abs(Hit.ImpactPoint.Z+4)<.15);
        TestTrue(TEXT("Bevel collision normal slopes upward"),Hit.ImpactNormal.Z>.5 && Hit.ImpactNormal.Z<.95);
        TestTrue(TEXT("Flat center remains at Z zero"),Trace({13,17},Hit) && FMath::Abs(Hit.ImpactPoint.Z)<.01);
        if (Mode==EPlatformMode::Cellular)
        {
            TestTrue(TEXT("Beveled edge cell is destructible"),P->DamageCellAtWorldLocation({496,13,-4},10000));
            TestFalse(TEXT("Destroyed bevel cell removes its collision immediately"),Trace({496,13},Hit));
            P->RebuildPlatform();
            TestFalse(TEXT("Rebuild retains destruction state"),Trace({496,13},Hit));
            P->RestoreAllCells();
            TestTrue(TEXT("Repair restores the bevel collision"),Trace({496,13},Hit));
            P->SetCanBeDamaged(false);
            TestFalse(TEXT("Actor's existing damage switch blocks direct cell damage"),P->DamageCellAtWorldLocation({13,17,0},10000));
            P->SetCanBeDamaged(true);
        }
    }
    // Every extended silhouette, holes, narrow organic contours, both geometry modes and patterns.
    for (auto Mode:{EPlatformMode::Solid,EPlatformMode::Cellular})
    for (auto Shape:{EChunkedDecorativeFloorShape::Grid,EChunkedDecorativeFloorShape::UShape,
        EChunkedDecorativeFloorShape::Ring,EChunkedDecorativeFloorShape::PiercedDiamond,
        EChunkedDecorativeFloorShape::RoundedBlob,EChunkedDecorativeFloorShape::Ellipse,
        EChunkedDecorativeFloorShape::RegularPolygon,EChunkedDecorativeFloorShape::ParametricShape})
    {
        P->bRoundCorners=true; P->CornerRadius=60;
        P->FloorPattern=EChunkedFloorPattern::Solid;
        P->ConfigurePlatform(Shape,{1100,1000},80,Mode);
        const FString Context=FString::Printf(TEXT("shape %d mode %d"),int32(Shape),int32(Mode));
        TestTrue(Context+TEXT(" retains a safe positive bevel"),P->EffectiveBevelSize>0);
        const FGeneralPolygon2d Footprint=P->GetPlatformPolygon();
        bool Valid=true, Sloped=false; double ProjectedTop=0, VerticalArea=0;
        for (const auto& C:P->ChunkComponents)
        {
            const auto& Mesh=*C->GetMesh(); Valid &= Mesh.CheckValidity();
            for (int32 ID:Mesh.TriangleIndicesItr())
            {
                FVector3d A,B,CV; Mesh.GetTriVertices(ID,A,B,CV);
                Valid &= !A.ContainsNaN() && !B.ContainsNaN() && !CV.ContainsNaN() && Mesh.GetTriArea(ID)>1.e-8;
                const auto NI=Mesh.Attributes()->PrimaryNormals()->GetTriangle(ID);
                const auto N=Mesh.Attributes()->PrimaryNormals()->GetElement(NI.A);
                if (N.Z>.01) ProjectedTop+=FMath::Abs((B-A).Cross(CV-A).Z)*.5;
                if (FMath::Abs(N.Z)<.001)
                {
                    const FVector3d Center=(A+B+CV)/3;
                    int Hole,Segment; double Alpha;
                    // Cellular geometry may contain grid-cut caps; only the actual
                    // outline (including holes) is expected to equal perimeter * height.
                    if (Footprint.DistanceSquared(FVector2D(Center.X,Center.Y),Hole,Segment,Alpha)<1.e-7)
                        VerticalArea+=Mesh.GetTriArea(ID);
                }
                Sloped |= N.Z>.05 && N.Z<.95;
            }
        }
        TestTrue(Context+TEXT(" has valid finite nondegenerate mesh and sloped faces"),Valid && Sloped);
        const double Expected=FMath::Abs(P->GetPlatformPolygon().SignedArea());
        TestTrue(Context+TEXT(" has no missing or overlapping upper surface"),FMath::Abs(ProjectedTop-Expected)<FMath::Max(1.,Expected*.00002));
        double Perimeter=0;
        P->ForEachShapeBoundary([&](const TArray<FVector2D>& Loop)
        {
            for (int32 I=0; I<Loop.Num(); ++I) Perimeter+=(Loop[(I+1)%Loop.Num()]-Loop[I]).Size();
        });
        double ExpectedSides=Perimeter*(P->Thickness-2*P->EffectiveBevelSize);
        if (Mode==EPlatformMode::Cellular)
        {
            // The inherited cellular mesh also has coincident fragment/cap edges on
            // some curved silhouettes. Compare against that unmodified source to
            // isolate lost/duplicated wall area introduced by the bevel operation.
            double BaselineArea=0;
            for (int32 Chunk=0;Chunk<P->ChunkComponents.Num();++Chunk)
            {
                FDynamicMesh3 Baseline;
                P->AChunkedDestructibleDecorativeFloor::BuildChunkMesh(Chunk,Baseline);
                for (int32 ID:Baseline.TriangleIndicesItr())
                {
                    const auto NI=Baseline.Attributes()->PrimaryNormals()->GetTriangle(ID);
                    if (FMath::Abs(Baseline.Attributes()->PrimaryNormals()->GetElement(NI.A).Z)>.001) continue;
                    const auto Center=Baseline.GetTriCentroid(ID);
                    int H,S; double T;
                    if (Footprint.DistanceSquared(FVector2D(Center.X,Center.Y),H,S,T)<1.e-7) BaselineArea+=Baseline.GetTriArea(ID);
                }
            }
            ExpectedSides=BaselineArea*(P->Thickness-2*P->EffectiveBevelSize)/P->Thickness;
        }
        TestTrue(Context+FString::Printf(TEXT(" side walls remain complete after bevel clipping (%.3f / %.3f)"),VerticalArea,ExpectedSides),FMath::Abs(VerticalArea-ExpectedSides)<FMath::Max(1.,ExpectedSides*.00002));
        if (P->CachedShapeHoles.Num()>0)
        {
            FVector2D HoleCenter=FVector2D::ZeroVector;
            for (auto V:P->CachedShapeHoles[0]) HoleCenter+=V;
            HoleCenter/=P->CachedShapeHoles[0].Num();
            FHitResult Hit;
            TestFalse(Context+TEXT(" bevel never seals a hole"),Trace(HoleCenter,Hit));
        }
        for (auto Pattern:{EChunkedFloorPattern::Checkerboard,EChunkedFloorPattern::HeartMotif,EChunkedFloorPattern::CirclesAndPaths})
        {
            P->FloorPattern=Pattern; P->BorderStyle=EPlatformBorder::Rim; P->BorderWidth=18; P->ApplyMaterialPattern();
            bool PatternInside=true;
            for (const auto& C:P->ChunkComponents) for (int32 ID:C->GetMesh()->TriangleIndicesItr())
            {
                const auto& Mesh=*C->GetMesh(); FVector3d A,B,CV; Mesh.GetTriVertices(ID,A,B,CV);
                if (FMath::Min3(A.Z,B.Z,CV.Z)<-.001) continue;
                const auto Center=(A+B+CV)/3; int H,S; double T;
                const FVector2D XY(Center.X,Center.Y);
                PatternInside &= P->BevelTopPolygon.Contains(XY) || P->BevelTopPolygon.DistanceSquared(XY,H,S,T)<.01;
            }
            TestTrue(Context+TEXT(" surface painting stays off the bevel"),PatternInside);
        }
        P->BorderStyle=EPlatformBorder::None;
    }
    auto* Lobby=World->SpawnActor<AParametricLobbyPlatformDestructable>();
    Lobby->SetActorLocation({20000,0,0}); Lobby->Width=1200; Lobby->Depth=1000;
    Lobby->Pads[0].ActorClass=AStaticMeshActor::StaticClass(); Lobby->Pads[0].SpawnOffset={0,0,30};
    TestTrue(TEXT("Lobby settings import"),P->CopyPlatformSettingsFrom(Lobby));
    TestTrue(TEXT("Lobby uses existing rim, palette, neon and pattern controls"),P->BorderStyle==EPlatformBorder::Rim &&
        P->BodyMaterialIndex==2 && P->SideNeon.Mode==EFloorSideNeonMode::Stripe &&
        P->SideNeon.Scope==EFloorSideNeonScope::AllExposedSides && P->FloorPattern==EChunkedFloorPattern::CirclesAndPaths);
    TestTrue(TEXT("Lobby animations transfer without a second animation system"),P->bReactToMusic && P->PlanarAnimationType==EPlanarAnimationType::FlowingWave);
    TestTrue(TEXT("No source transform is copied"),P->GetActorLocation().IsNearlyZero());
    TestEqual(TEXT("Only visible pads are spawn sockets"),P->GetPadIds().Num(),3);
    Lobby->Destroy();
    P->bUseDestructionRipple=false; P->FlashDuration=0; P->bPlayDestroyTickSound=false;
    P->PadPathSettings.bSpawnOnBeginPlay=false;
    P->DispatchBeginPlay();
    P->SetActorRotation(FRotator(0,35,0)); P->SetActorScale3D({1.5,1.2,1});
    FTransform SpawnTransform; TestTrue(TEXT("Fitted pad provides a transformed spawn socket"),P->GetPadSpawnTransform(TEXT("Center"),SpawnTransform));
    AActor* Spawn=P->SpawnActorAtPad(TEXT("Center"),nullptr);
    TestNotNull(TEXT("Configured pad actor spawns"),Spawn);
    if (Spawn)
    {
        TestTrue(TEXT("Spawn uses fitted center and offset"),Spawn->GetActorTransform().Equals(SpawnTransform,.01));
        TestTrue(TEXT("Spawn attaches to platform"),Spawn->GetAttachParentActor()==P);
        TestTrue(TEXT("Repeated spawn returns existing actor"),P->SpawnActorAtPad(TEXT("Center"),nullptr)==Spawn);
        FVector Center; P->GetPadWorldCenter(TEXT("Center"),Center);
        TestTrue(TEXT("Destroy supporting cell"),P->DamageCellAtWorldLocation(Center,10000));
        TestNull(TEXT("Unsupported pad actor detaches"),Spawn->GetAttachParentActor());
        TestNull(TEXT("Broken pad refuses new spawns"),P->SpawnActorAtPad(TEXT("Center"),nullptr));
        P->ClearSpawnedActors(); TestTrue(TEXT("Pad cleanup destroys its owned actor"),Spawn->IsActorBeingDestroyed());
    }
    P->RestoreAllCells(); P->PadPathSettings.SurfaceMaterial=nullptr; P->RebuildPlatform();
    TestTrue(TEXT("Gameplay sockets still resolve without a visual material"),P->GetPadIds().Num()==3 && P->IsPadSurfaceIntact(TEXT("Center")));
    TestNull(TEXT("Hidden path anchor cannot spawn"),P->SpawnActorAtPad(TEXT("Entrance"),AStaticMeshActor::StaticClass()));
    P->PadPathSettings.bEnableSpawning=false;
    TestNull(TEXT("Master spawn toggle blocks manual calls"),P->SpawnActorAtPad(TEXT("Center"),AStaticMeshActor::StaticClass()));
    P->PadPathSettings.bEnableSpawning=true;
    P->SetPlatformMode(EPlatformMode::Solid);
    TestTrue(TEXT("Solid platform pads are intact without allocating destruction cells"),P->IsPadSurfaceIntact(TEXT("Center")) && P->GetDestructibleCellCount()==0);
    TestNotNull(TEXT("Solid platform pads can spawn actors"),P->SpawnActorAtPad(TEXT("Center"),nullptr));
    P->ClearSpawnedActors();
    P->PadPathSettings.bFitPadsInsidePlatform=false;
    P->PadPathSettings.Pads[0].Position={1,1};
    P->RebuildPlatform();
    TestFalse(TEXT("A pad outside the rounded solid silhouette has no supporting surface"),P->IsPadSurfaceIntact(TEXT("Center")));
    TestNull(TEXT("A pad outside the solid surface refuses spawns"),P->SpawnActorAtPad(TEXT("Center"),nullptr));
    P->SetActorTransform(FTransform::Identity);
    P->bBevelEdges=false; P->bRoundCorners=false; P->FloorPattern=EChunkedFloorPattern::Solid;
    P->ConfigurePlatform(EChunkedDecorativeFloorShape::Grid,{1000,800},80,EPlatformMode::Cellular);
    FHitResult Hit; Trace({13,17},Hit);
    FPointDamageEvent Damage(10000,Hit,FVector(0,0,-1),nullptr);
    TestTrue(TEXT("Standard point damage routes to existing cell system"),P->TakeDamage(10000,Damage,nullptr,nullptr)>0);
    TestFalse(TEXT("Point damage removes collision"),Trace({13,17},Hit));
    // Actual Details tree honors flags (not just UPROPERTY metadata).
    P->CornerRadius=100;
    auto VisibleNames=[&]()
    {
        auto& Editor=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        auto Rows=Editor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs()); Rows->SetObjects({P});
        TSet<FName> Names;
        TFunction<void(TSharedRef<IDetailTreeNode>)> Visit=[&](auto Node)
        {
            if (auto H=Node->CreatePropertyHandle(); H && H->GetProperty()) Names.Add(H->GetProperty()->GetFName());
            TArray<TSharedRef<IDetailTreeNode>> Children; Node->GetChildren(Children);
            for (auto Child:Children) Visit(Child);
        };
        for (auto Root:Rows->GetRootTreeNodes()) Visit(Root);
        return Names;
    };
    auto Hidden=VisibleNames();
    TestFalse(TEXT("Bevel size is hidden while disabled"),Hidden.Contains(TEXT("BevelSize")));
    TestFalse(TEXT("Corner radius is hidden while disabled"),Hidden.Contains(TEXT("CornerRadius")));
    P->bBevelEdges=true; P->bRoundCorners=true;
    auto Shown=VisibleNames();
    TestTrue(TEXT("Both active flags expose their parameters"),Shown.Contains(TEXT("BevelSize")) && Shown.Contains(TEXT("CornerRadius")) && Shown.Contains(TEXT("CornerSegments")));
    auto* Unrelated=World->SpawnActor<AStaticMeshActor>();
    TestFalse(TEXT("Unrelated actors cannot supply platform settings"),P->CopyPlatformSettingsFrom(Unrelated));
    TestTrue(TEXT("Rejected settings import leaves the finish untouched"),P->bBevelEdges && P->bRoundCorners);
    Unrelated->Destroy();
    return true;
}
#endif
