#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricPlatformCity.h"
#include "ParametricPlatform.h"
#include "PlatformPolygonGeometry.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Curve/PolygonOffsetUtils.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

using namespace UE::Geometry;
namespace
{
    double TotalArea(const TArray<FGeneralPolygon2d>& Polys) { double A = 0; for (const auto& P : Polys) A += P.SignedArea(); return A; }
    FGeneralPolygon2d WorldPolygon(const FPlatformCityPlot& Plot, bool Source = false)
    {
        TArray<FVector2D> V = Source ? Plot.SourceOutline : Plot.Outline;
        for (auto& P : V) P += Plot.Center;
        return FGeneralPolygon2d(FPolygon2d(V));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlatformCityPlannerTest, "MyProject6.PlatformCity.SharedRoadNetwork200Seeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlatformCityPlannerTest::RunTest(const FString&)
{
    FPlatformCityRules Rules; FString Error;
    TSet<FString> Signatures; int32 Minimum = 1000, Maximum = 0, Curved = 0, SideRoads = 0;
    for (int32 Seed = 0; Seed < 200; ++Seed)
    {
        FPlatformCityPlan Plan, Repeat;
        if (!PlatformCity::MakePlan(Rules, Seed, Plan, Error)) { AddError(FString::Printf(TEXT("Seed %d: %s"), Seed, *Error)); return false; }
        PlatformCity::MakePlan(Rules, Seed, Repeat, Error);
        TestEqual(TEXT("Deterministic seed"), Plan.Signature, Repeat.Signature); Signatures.Add(Plan.Signature);
        Minimum = FMath::Min(Minimum, Plan.Plots.Num()); Maximum = FMath::Max(Maximum, Plan.Plots.Num());
        TestEqual(TEXT("3 vertical main roads"), Plan.Roads.FilterByPredicate([](const auto& R) { return R.Kind == EPlatformCityRoadKind::VerticalMain; }).Num(), 3);
        TestEqual(TEXT("2 horizontal main roads"), Plan.Roads.FilterByPredicate([](const auto& R) { return R.Kind == EPlatformCityRoadKind::HorizontalMain; }).Num(), 2);
        TArray<FGeneralPolygon2d> Roads, Network;
        for (const auto& Road : Plan.Roads)
        {
            Roads.Emplace(FPolygon2d(Road.Outline));
            SideRoads += Road.Kind == EPlatformCityRoadKind::Side;
            if (Road.Kind != EPlatformCityRoadKind::Side)
            {
                const auto A = Road.Points[0], B = Road.Points.Last();
                TestTrue(TEXT("Main road reaches both boundary edges"), Road.Kind == EPlatformCityRoadKind::VerticalMain ?
                    FMath::Abs(A.Y - B.Y) > 4999.9 : FMath::Abs(A.X - B.X) > 7999.9);
                const auto Mid = Road.Points[Road.Points.Num()/2];
                Curved += FMath::Abs(FVector2D::CrossProduct((B-A).GetSafeNormal(), Mid-A)) > 5;
            }
        }
        TestTrue(TEXT("Road union"), PolygonsUnion(Roads, Network, false));
        TestEqual(TEXT("Every side road belongs to the connected main network"), Network.Num(), 1);
        double SourceArea = 0; FBox2D Envelope(ForceInit);
        TArray<FGeneralPolygon2d> Plates;
        for (const auto& Plot : Plan.Plots)
        {
            const auto Poly = WorldPolygon(Plot), Raw = WorldPolygon(Plot, true);
            Plates.Add(Poly); SourceArea += Raw.SignedArea();
            for (const auto& P : Raw.GetOuter().GetVertices()) Envelope += P;
            TestTrue(TEXT("Free polygon boundary"), Plot.SourceOutline.Num() >= 4 && Plot.Outline.Num() >= 4);
            for (const auto& P : Poly.GetOuter().GetVertices()) TestTrue(TEXT("City boundary respected"), FMath::Abs(P.X) <= 4000.01 && FMath::Abs(P.Y) <= 2500.01);
            TArray<FGeneralPolygon2d> Intersection, Outside;
            PolygonsIntersection(TArray<FGeneralPolygon2d>{Poly}, Network, Intersection);
            TestTrue(FString::Printf(TEXT("Seed %d: plate/road intersection %.6f cm2"), Seed, TotalArea(Intersection)), TotalArea(Intersection) < 10);
            PolygonsDifference(TArray<FGeneralPolygon2d>{Poly}, TArray<FGeneralPolygon2d>{Raw}, Outside);
            TestTrue(TEXT("Rounding stays inside subtraction result"), TotalArea(Outside) < 10);
        }
        for (int32 I = 0; I < Plates.Num(); ++I) for (int32 J = 0; J < I; ++J)
        {
            if (!Plates[I].Bounds().Intersects(Plates[J].Bounds())) continue;
            TArray<FGeneralPolygon2d> Overlap;
            PolygonsIntersection(TArray<FGeneralPolygon2d>{Plates[I]}, TArray<FGeneralPolygon2d>{Plates[J]}, Overlap);
            TestTrue(TEXT("No overlapping plates"), TotalArea(Overlap) < 1);
        }
        TestTrue(FString::Printf(TEXT("Seed %d: partition area error %.6f cm2"), Seed, SourceArea + TotalArea(Network) - 8000. * 5000.), FMath::Abs(SourceArea + TotalArea(Network) - 8000. * 5000.) < 50);
        TestTrue(TEXT("Exact city envelope"), Envelope.GetSize().Equals(FVector2D(8000,5000), .05));
        TestTrue(TEXT("Bounded platform budget"), Plan.Plots.Num() <= Rules.MaxPlates);
    }
    TestEqual(TEXT("Unique seeds"), Signatures.Num(), 200);
    TestTrue(TEXT("Curved main roads generated"), Curved > 400);
    TestTrue(TEXT("Side roads generated"), SideRoads > 1000);
    FPlatformCityPlan Sentinel; Sentinel.Signature = TEXT("unchanged");
    Rules.MainRoadWidth = 20000;
    TestFalse(TEXT("Invalid actor settings rejected"), PlatformCity::MakePlan(Rules, 1, Sentinel, Error));
    TestEqual(TEXT("Failure preserves old layout"), Sentinel.Signature, FString(TEXT("unchanged")));
    Rules = FPlatformCityRules(); Rules.RoadCurvature = 0; Rules.SideRoadBend = 0; Rules.CornerRadius = 0;
    Rules.Width = 9000; Rules.Height = 6200; Rules.VerticalRoads = 4;
    TestTrue(TEXT("Editable straight roads and sharp corners"), PlatformCity::MakePlan(Rules, 8, Sentinel, Error));
    AddInfo(FString::Printf(TEXT("200 unique curved layouts; %d..%d plates; %d side roads; full land/road partition verified."), Minimum, Maximum, SideRoads));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlatformCityAssemblyTest, "MyProject6.PlatformCity.GeometryOwnershipAndBeginPlay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlatformCityAssemblyTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* City = World->SpawnActor<AParametricPlatformCity>();
    auto* Unrelated = World->SpawnActor<AActor>();
    TestNull(TEXT("No external rules property remains"), City->GetClass()->FindPropertyByName(TEXT("RulesFile")));
    const auto* Property = City->GetClass()->FindPropertyByName(TEXT("Rules"));
    TestTrue(TEXT("Actor settings are editable"), Property && Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst));
    if (!City->GenerateWithSeed(42)) { AddError(City->LastError); return false; }
    const int32 Count = City->GetPlatforms().Num(); const FString Signature = City->GeneratedPlan.Signature;
    for (auto* Platform : City->GetPlatforms())
    {
        TestTrue(TEXT("Owned and attached"), Platform->GetOwner() == City && Platform->GetAttachParentActor() == City);
        TestEqual(TEXT("No catalog shapes used"), Platform->GetPlatformShape(), EChunkedDecorativeFloorShape::DynamicPolygon);
        TArray<UDynamicMeshComponent*> Meshes; Platform->GetComponents(Meshes);
        bool Geometry = false;
        for (auto* Mesh : Meshes) if (Mesh->GetMesh()->TriangleCount() > 0)
        { Geometry = true; TestTrue(TEXT("Valid platform mesh"), Mesh->GetMesh()->CheckValidity()); }
        TestTrue(TEXT("Actual geometry exists"), Geometry);
    }
    for (int32 I = 0; I < 30; ++I)
    {
        FTransform T; AParametricPlatform* Support = nullptr;
        if (!TestTrue(TEXT("Safe spawn sample"), City->FindSpawnTransform(55, 25, I, T, Support))) return false;
        for (int32 J = 0; J < 9; ++J)
        {
            const double A = J * UE_DOUBLE_TWO_PI / 8;
            const FVector P = T.GetLocation() + (J == 8 ? FVector::ZeroVector : FVector(FMath::Cos(A), FMath::Sin(A), 0) * 80);
            FHitResult Hit;
            TestTrue(TEXT("Entire spawn footprint supported"), World->LineTraceSingleByChannel(Hit, P+FVector(0,0,30), P-FVector(0,0,30), ECC_Visibility));
            TestTrue(TEXT("Correct support actor"), Hit.GetActor() == Support);
        }
    }
    for (const auto& Road : City->GeneratedPlan.Roads)
        for (int32 I = 1; I < Road.Points.Num(); ++I)
        {
            const auto Direction = (Road.Points[I]-Road.Points[I-1]).GetSafeNormal();
            const FVector2D Mid = (Road.Points[I]+Road.Points[I-1])*.5;
            for (double Offset : {-.45, 0., .45})
            {
                const FVector P(Mid + FVector2D(-Direction.Y, Direction.X)*Road.Width*Offset, 0);
                FHitResult Hit;
                TestFalse(TEXT("Road width has no platform collision"), World->LineTraceSingleByChannel(Hit,P+FVector(0,0,20),P-FVector(0,0,100),ECC_Visibility));
            }
        }
    TestTrue(TEXT("Rebuild"), City->GenerateWithSeed(42));
    TestEqual(TEXT("Same signature"), City->GeneratedPlan.Signature, Signature);
    TestEqual(TEXT("No duplication"), City->GetPlatforms().Num(), Count);
    City->Rules.Width = -1;
    TestFalse(TEXT("Invalid property retains preview"), City->GenerateNewLayout());
    TestEqual(TEXT("Old actors retained"), City->GetPlatforms().Num(), Count);
    City->Rules.Width = 9000; City->Rules.CornerRadius = 160;
    City->SetActorTransform(FTransform(FRotator(0,27,0),FVector(1200,-700,200)));
    TestTrue(TEXT("Property-edited transformed city"), City->GenerateWithSeed(82));
    FTransform Sample; AParametricPlatform* Support = nullptr; City->FindSpawnTransform(75,30,2,Sample,Support);
    FHitResult Hit;
    TestTrue(TEXT("Transformed support"), World->LineTraceSingleByChannel(Hit,Sample.GetLocation()+FVector(0,0,30),Sample.GetLocation()-FVector(0,0,30),ECC_Visibility));
    TestFalse(TEXT("Oversized content rejected"), City->FindSpawnTransform(10000,10,2,Sample,Support));
    const int32 Before = City->Seed; World->BeginPlay();
    TestTrue(TEXT("New seed on BeginPlay"), City->Seed != Before);
    TestTrue(TEXT("BeginPlay successful"), City->LastError.IsEmpty());
    TestEqual(TEXT("Runtime uses edited width"), City->AppliedRules.Width, 9000.f);
    int32 Actual = 0;
    for (TActorIterator<AParametricPlatform> It(World); It; ++It) if (!It->IsActorBeingDestroyed() && It->GetOwner()==City) ++Actual;
    TestEqual(TEXT("No old runtime platforms"), Actual, City->GetPlatforms().Num());
    City->ClearGenerated(); TestEqual(TEXT("Clear"), City->GetPlatforms().Num(), 0);
    TestTrue(TEXT("Unrelated actor preserved"), IsValid(Unrelated) && !Unrelated->IsActorBeingDestroyed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlatformDynamicPolygonTest, "MyProject6.PlatformCity.FreePolygonMeshAndRounding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlatformDynamicPolygonTest::RunTest(const FString&)
{
    const TArray<FVector2D> Rectangle = {{-500,-400},{500,-400},{500,400},{-500,400}};
    TArray<FVector2D> Coarse, Smooth, Larger; FString Error;
    TestTrue(TEXT("Coarse rounded outline"), PlatformPolygonGeometry::BuildOutline(Rectangle,100,2,Coarse,Error));
    TestTrue(TEXT("Smooth rounded outline"), PlatformPolygonGeometry::BuildOutline(Rectangle,100,16,Smooth,Error));
    TestTrue(TEXT("CornerSegments changes arc resolution"), Smooth.Num() >= 60 && Smooth.Num() > Coarse.Num());
    const double RoundedArea = 1000. * 800. - (4. - UE_DOUBLE_PI) * 100. * 100.;
    TestTrue(TEXT("Corners follow circular arcs, not chamfers"), FMath::Abs(FPolygon2d(Smooth).Area() - RoundedArea) < 300.);
    TestTrue(TEXT("Larger corner radius"), PlatformPolygonGeometry::BuildOutline(Rectangle,160,16,Larger,Error));
    TestTrue(TEXT("Radius controls rounding"), FPolygon2d(Larger).Area() < FPolygon2d(Smooth).Area());
    UWorld* World = UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Platform = World->SpawnActor<AParametricPlatform>(); Platform->BorderStyle = EPlatformBorder::None;
    const TArray<FVector2D> Raw = {{-800,-500},{700,-400},{620,500},{250,550},{180,-80},{-180,-120},{-270,450},{-750,350}};
    for (const auto Mode : {EPlatformMode::Solid,EPlatformMode::Cellular})
    {
        Platform->PlatformMode = Mode;
        for (float Radius : {0.f,100.f})
        {
            TestTrue(TEXT("Arbitrary concave polygon builds"), Platform->ConfigurePolygon(Raw,80,Radius,8));
            TestTrue(TEXT("No geometry warning"), Platform->ShapeWarning.IsEmpty());
            FHitResult Hit;
            TestFalse(TEXT("Concave notch remains empty"), World->LineTraceSingleByChannel(Hit,FVector(0,220,30),FVector(0,220,-120),ECC_Visibility));
            TestTrue(TEXT("Actual interior has collision"), World->LineTraceSingleByChannel(Hit,FVector(0,-300,30),FVector(0,-300,-120),ECC_Visibility));
            TArray<UDynamicMeshComponent*> Meshes; Platform->GetComponents(Meshes);
            for (auto* Mesh : Meshes) if (Mesh->GetMesh()->TriangleCount()>0)
            {
                TestTrue(TEXT("Valid free mesh"), Mesh->GetMesh()->CheckValidity());
                if (Mode==EPlatformMode::Solid) TestTrue(TEXT("Watertight slab"), Mesh->GetMesh()->IsClosed());
            }
        }
    }
    const auto Before = Platform->PolygonOutline;
    TestFalse(TEXT("Self-crossing outline rejected"), Platform->ConfigurePolygon({{-500,-500},{500,500},{-500,500},{500,-500}},80,0,8));
    TestTrue(TEXT("Bad geometry retains previous settings"), Before==Platform->PolygonOutline);
    return true;
}
#endif

