#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ProceduralCityGenerator.h"
#include "ParametricCityLink.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Chaos/Convex.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityPlannerTest,"MyProject6.City.PlannerRules300Seeds",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCityPlannerTest::RunTest(const FString&)
{
    const auto Catalog=GetDefault<AProceduralCityGenerator>()->BuildingCatalog;
    TSet<FString> Signatures;int32 TotalBuildings=0,Stairs=0,Ramps=0;
    for (ECityLayout Mode:{ECityLayout::Radial,ECityLayout::Grid,ECityLayout::Organic})
    for (int32 Seed=0;Seed<100;++Seed)
    {
        FProceduralCityRules R;R.Layout=Mode;R.DistrictCount=3+Seed%10;
        FProceduralCityPlan P,Q;FString Error;
        if (!TestTrue(FString::Printf(TEXT("Mode %d seed %d plan: %s"),int32(Mode),Seed,*Error),ProceduralCity::MakePlan(R,Seed,Catalog,{},P,Error)))
        {AddError(Error);return false;}
        TestTrue(TEXT("Independent rule validation"),ProceduralCity::ValidatePlan(R,P,Error));
        TestTrue(TEXT("Same-seed regeneration"),ProceduralCity::MakePlan(R,Seed,Catalog,{},Q,Error));
        TestEqual(TEXT("Same rules and seed produce identical plan"),P.Signature,Q.Signature);
        Signatures.Add(P.Signature);TotalBuildings+=P.Buildings.Num();
        for (const auto& C:P.Connections){Stairs+=C.Kind==ECityLinkKind::Stairs;Ramps+=C.Kind==ECityLinkKind::Ramp;}
        // Verify the independent entry point is outside all building discs.
        for (const auto& B:P.Buildings) TestTrue(TEXT("Player entry stays clear of buildings"),FVector2D(B.Center-P.EntryLocation).Length()>B.Radius+34);
    }
    TestTrue(TEXT("Layouts vary across seeds and modes"),Signatures.Num()>280);
    TestTrue(TEXT("Catalog is used throughout the generated cities"),TotalBuildings>1000);
    TestTrue(TEXT("Elevation mix produces both stairs and ramps"),Stairs>30 && Ramps>30);
    FProceduralCityRules R;FProceduralCityPlan Sentinel;Sentinel.Signature=TEXT("KeepPrevious");FString Error;
    R.RoadWidth=-1;TestFalse(TEXT("Invalid rules fail before mutation"),ProceduralCity::MakePlan(R,1,Catalog,{},Sentinel,Error));
    TestEqual(TEXT("Failed plan leaves previous data intact"),Sentinel.Signature,FString(TEXT("KeepPrevious")));
    R=FProceduralCityRules();FCityExclusionZone E;E.Radius=5000;
    TestFalse(TEXT("An excluded hub is rejected explicitly"),ProceduralCity::MakePlan(R,1,Catalog,{E},Sentinel,Error));
    R.ElevationStep=1500;R.ElevationLevels=5;R.MaxRampSlope=1;R.DistrictGap=600;
    TestFalse(TEXT("Impossible slope network is not silently disconnected"),ProceduralCity::MakePlan(R,2,Catalog,{},Sentinel,Error));
    AddInfo(FString::Printf(TEXT("300 plans: %d unique signatures, %d buildings, %d stairs, %d ramps"),Signatures.Num(),TotalBuildings,Stairs,Ramps));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityAssemblyTest,"MyProject6.City.AssemblyCollisionOwnershipRuntime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCityAssemblyTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* G=World->SpawnActor<AProceduralCityGenerator>();
    auto* Unrelated=World->SpawnActor<AActor>();
    G->BuildingCatalog.Reset();G->Rules.DistrictCount=3;G->Rules.bHubLandmark=false;G->bGenerateOnBeginPlay=false;
    if (!TestTrue(TEXT("Infrastructure assembly succeeds"),G->GenerateWithSeed(42))){AddError(G->LastError);return false;}
    const int32 Count=G->GetGeneratedActors().Num();const FString Signature=G->GeneratedPlan.Signature;
    for (FName Button:{FName("BuildPreview"),FName("RandomizePreview"),FName("CheckPreview"),FName("ImportRules"),FName("MeasureCatalog"),FName("ClearGenerated")})
    {
        UFunction* F=G->FindFunction(Button);
        TestTrue(TEXT("Editor buttons have no input or return parameters"),F && F->NumParms==0 && F->HasMetaData(TEXT("CallInEditor")));
    }
    FCityBuildingCatalogEntry Recursive;Recursive.ActorClass=AProceduralCityGenerator::StaticClass();G->BuildingCatalog.Add(Recursive);
    TestFalse(TEXT("Recursive generator catalog is rejected before spawn"),G->RebuildSameSeed());
    TestEqual(TEXT("Rejected catalog retains the city"),G->GetGeneratedActors().Num(),Count);G->BuildingCatalog.Reset();
    FHitResult Hit;
    for (const auto& C:G->GeneratedPlan.Connections)
    {
        for (int32 I=0;I<C.Points.Num();++I)
        {
            const FVector P=C.Points[I];
            TestTrue(TEXT("Bridge/ramp/stair centreline has continuous collision"),World->LineTraceSingleByChannel(Hit,P+FVector(0,0,40),P-FVector(0,0,45),ECC_Visibility));
        }
    }
    TestTrue(TEXT("Entry has supporting geometry"),World->LineTraceSingleByChannel(Hit,G->GetEntryTransform().GetLocation(),G->GetEntryTransform().GetLocation()-FVector(0,0,180),ECC_Visibility));
    TestTrue(TEXT("Repeated generation works"),G->RebuildSameSeed());
    TestEqual(TEXT("Regeneration does not duplicate owned actors"),G->GetGeneratedActors().Num(),Count);
    TestEqual(TEXT("Saved seed remains deterministic"),G->GeneratedPlan.Signature,Signature);
    G->Rules.RoadWidth=-1;TestFalse(TEXT("Invalid edit is rejected"),G->RebuildSameSeed());
    TestEqual(TEXT("Failed rebuild retains existing actors"),G->GetGeneratedActors().Num(),Count);G->Rules.RoadWidth=420;
    auto* Other=World->SpawnActor<AProceduralCityGenerator>(FVector(100000,0,0),FRotator::ZeroRotator);
    Other->BuildingCatalog.Reset();Other->Rules.DistrictCount=3;Other->bGenerateOnBeginPlay=false;
    TestTrue(TEXT("Second independent generator builds"),Other->GenerateWithSeed(45));const int32 OtherCount=Other->GetGeneratedActors().Num();
    G->ClearGenerated();TestEqual(TEXT("Clear removes own city"),G->GetGeneratedActors().Num(),0);
    TestTrue(TEXT("Clear preserves manually placed actor"),IsValid(Unrelated) && !Unrelated->IsActorBeingDestroyed());
    TestEqual(TEXT("Clear preserves another city"),Other->GetGeneratedActors().Num(),OtherCount);
    G->SetActorTransform(FTransform(FRotator(0,31,0),FVector(500,-1200,200),FVector(1.2)));
    TestTrue(TEXT("Yaw/translated/uniformly scaled city builds"),G->GenerateWithSeed(23));
    TestTrue(TEXT("Transformed entry remains walkable"),World->LineTraceSingleByChannel(Hit,G->GetEntryTransform().GetLocation(),G->GetEntryTransform().GetLocation()-FVector(0,0,200),ECC_Visibility));
    G->SetActorScale3D({1,2,1});TestFalse(TEXT("Nonuniform scale rejected before invalidating constraints"),G->RebuildSameSeed());G->SetActorScale3D(FVector::OneVector);
    World->BeginPlay();TestTrue(TEXT("Runtime new-seed assembly works"),G->GenerateNewSeed());
    TestFalse(TEXT("Idle generator does not tick"),G->IsActorTickEnabled());
    World->EndPlay(EEndPlayReason::Quit);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityLinkTest,"MyProject6.City.LinkStairsRampsAndPaint",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCityLinkTest::RunTest(const FString&)
{
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);W->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT {W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
    auto* A=W->SpawnActor<AParametricCityLink>();A->Points={{0,0,0},{2200,0,360}};A->Kind=ECityLinkKind::Stairs;A->RebuildLink();
    auto* Comp=A->GetDynamicMeshComponent();
    TestTrue(TEXT("Link has a live physics body"),Comp->IsPhysicsStateCreated());
    TestTrue(TEXT("Deck hulls were installed"),Comp->GetBodySetup()->AggGeom.ConvexElems.Num()>20);
    for (const auto& H:Comp->GetBodySetup()->AggGeom.ConvexElems)
        if (!TestTrue(TEXT("Every road/rail hull cooks successfully"),H.GetChaosConvexMesh() && H.GetChaosConvexMesh()->IsValidGeometry())) return false;
    TestTrue(TEXT("Staircase meets its dimensions"),A->GenerationWarning.IsEmpty());TestEqual(TEXT("Maximum 18 cm rises"),A->GeneratedSteps,20);
    for (int32 I=0;I<20;++I)
    {
        FHitResult Hit;const FVector P((I+.5)*110,0,(I+1)*18);
        if (TestTrue(TEXT("Every stair tread has collision"),W->LineTraceSingleByChannel(Hit,P+FVector(0,0,5),P-FVector(0,0,5),ECC_Visibility)))
            TestTrue(TEXT("Physical tread height matches the visible step"),FMath::Abs(Hit.ImpactPoint.Z-P.Z)<.1);
    }
    A->Kind=ECityLinkKind::Ramp;A->RebuildLink();
    FHitResult Hit;TestTrue(TEXT("Ramp midpoint supports player"),W->LineTraceSingleByChannel(Hit,{1100,0,185},{1100,0,175},ECC_Visibility));
    TestTrue(TEXT("Ramp midpoint height interpolates endpoints"),FMath::Abs(Hit.ImpactPoint.Z-180)<.1);
    const int32 Tris=A->GetDynamicMeshComponent()->GetDynamicMesh()->GetTriangleCount();
    A->Points={{0,0,0},{10,0,360}};A->Kind=ECityLinkKind::Stairs;A->RebuildLink();
    TestFalse(TEXT("Impossible staircase reports an error"),A->GenerationWarning.IsEmpty());
    TestEqual(TEXT("Invalid staircase keeps previous mesh"),A->GetDynamicMeshComponent()->GetDynamicMesh()->GetTriangleCount(),Tris);
    return true;
}
#endif
