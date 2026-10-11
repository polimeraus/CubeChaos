#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricBallerinaRide.h"
#include "Components/ChildActorComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBallerinaRideTest,"MyProject6.Fairground.BallerinaRide",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBallerinaRideTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL()); World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Ride=World->SpawnActor<AParametricBallerinaRide>();
    TestEqual(TEXT("24 supplied seats"),Ride->GetGeneratedSeatCount(),24);
    TestEqual(TEXT("432 seam and 96 rim bulbs"),Ride->GetGeneratedBulbCount(),528);
    auto* Figure=Cast<ABallerinaRideFigure>(Ride->Figure->GetChildActor());
    auto CheckMaterials=[&](UStaticMesh* Asset)
    {
        if (!Asset) { return; }
        for (int32 I=0;I<Asset->GetStaticMaterials().Num();++I)
        {
            auto* Material=Asset->GetMaterial(I);
            TestTrue(TEXT("Imported mesh slots retain the authored material after reload"),Material && Material->GetPathName().StartsWith(TEXT("/Game/Fairground/BallerinaRide/Materials/")));
        }
    };
    if (TestNotNull(TEXT("Default figure actor"),Figure))
    { TestTrue(TEXT("Default figure has authored mesh"),Figure->Mesh->GetStaticMesh()!=nullptr); CheckMaterials(Figure->Mesh->GetStaticMesh()); }
    TInlineComponentArray<UChildActorComponent*> ChildComponents(Ride);
    for (auto* Child:ChildComponents)
    {
        if (auto* Seat=Cast<ABallerinaRideSeat>(Child->GetChildActor()))
        { TestTrue(TEXT("Default seat has authored mesh"),Seat->Mesh->GetStaticMesh()!=nullptr); CheckMaterials(Seat->Mesh->GetStaticMesh()); }
    }
    const auto* Mesh=Ride->Skirt->GetMesh();
    TestTrue(TEXT("Valid topology and overlays"),Mesh->CheckValidity());
    bool bUVs=true,bOutward=true;TSet<int32> Materials; int32 BadNormals=0;
    for (int32 T:Mesh->TriangleIndicesItr())
    {
        bUVs &= Mesh->Attributes()->PrimaryUV()->IsSetTriangle(T);
        bOutward &= Mesh->GetTriNormal(T).Z>0;
        if (Mesh->GetTriNormal(T).Z<=0 && BadNormals++<2)
        {
            FVector3d A,B,C; Mesh->GetTriVertices(T,A,B,C);
            AddInfo(FString::Printf(TEXT("Unexpected normal %s for %s / %s / %s"),*Mesh->GetTriNormal(T).ToString(),*A.ToString(),*B.ToString(),*C.ToString()));
        }
        Materials.Add(Mesh->Attributes()->GetMaterialID()->GetValue(T));
    }
    TestTrue(TEXT("Each surface has UVs"),bUVs);
    TestTrue(TEXT("All skirt triangles face up/out"),bOutward);
    TestEqual(TEXT("Every wedge has an independent material slot"),Materials.Num(),24);
    TestNearlyEqual(TEXT("Parametric skirt diameter"),Mesh->GetBounds().Max.X-Mesh->GetBounds().Min.X,1200.,.1);
    const int32 ComponentsBefore=Ride->GetComponents().Num();
    for (int32 I=0;I<3;++I) { Ride->RebuildRide(); }
    TestEqual(TEXT("Repeated reconstruction leaves no orphan components"),Ride->GetComponents().Num(),ComponentsBefore);
    TestEqual(TEXT("Repeated reconstruction leaves no duplicate seats"),Ride->GetGeneratedSeatCount(),24);

    Ride->InitialWaveAngle=0;Ride->InitialSpinAngle=0;Ride->WaveSpeed=90;Ride->SpinSpeed=0;Ride->TiltAngle=20;
    const FTransform P0=Ride->EvaluatePlatformTransform(0),P1=Ride->EvaluatePlatformTransform(1);
    TestTrue(TEXT("At t0 high edge is +X"),P0.TransformPosition(FVector(780,0,0)).Z>P0.TransformPosition(FVector(-780,0,0)).Z+400);
    TestTrue(TEXT("After quarter wave high edge is +Y"),P1.TransformPosition(FVector(0,780,0)).Z>P1.TransformPosition(FVector(0,-780,0)).Z+400);
    Ride->SpinSpeed=53;
    TestTrue(TEXT("Axial spin does not alter platform normal"),Ride->EvaluatePlatformTransform(1).GetRotation().RotateVector(FVector::UpVector).Equals(P1.GetRotation().RotateVector(FVector::UpVector),1e-5));
    TestTrue(TEXT("Axial spin actually rotates deck"),!Ride->EvaluatePlatformTransform(1).GetRotation().Equals(P1.GetRotation(),.01));
    for (int32 T=0;T<9;++T)
    {
        auto Pose=Ride->EvaluatePlatformTransform(T*.23);
        for (int32 A=0;A<72;++A)
        {
            const double Angle=A*2*PI/72;
            TestTrue(TEXT("Lowest deck edge clears floor at every wave phase"),Pose.TransformPosition(FVector(780*FMath::Cos(Angle),780*FMath::Sin(Angle),-22)).Z>=Ride->GroundClearance-.01);
        }
    }
    Ride->ResetMotion();Ride->SetRideRunning(true);Ride->Tick(.5f);
    TestTrue(TEXT("Runtime tick advances motion"),Ride->PlatformPivot->GetRelativeTransform().Equals(Ride->EvaluatePlatformTransform(.5),1e-4));
    const auto Paused=Ride->PlatformPivot->GetRelativeTransform();Ride->SetRideRunning(false);Ride->Tick(1);
    TestTrue(TEXT("Pause retains current pose"),Ride->PlatformPivot->GetRelativeTransform().Equals(Paused));
    Ride->ResetMotion();TestTrue(TEXT("Reset returns initial pose"),Ride->PlatformPivot->GetRelativeTransform().Equals(Ride->EvaluatePlatformTransform(0)));

    FBallerinaSliceOverride Override;Override.SliceIndex=3;Override.bShowLights=false;Override.bOverrideColor=true;Override.Color=FLinearColor::Green;
    Ride->SliceOverrides={Override};Ride->bShowRimLights=false;Ride->SeatCount=12;Ride->RebuildRide();
    TestEqual(TEXT("Seat count rebuild"),Ride->GetGeneratedSeatCount(),12);
    TestEqual(TEXT("Per-seam disable"),Ride->GetGeneratedBulbCount(),23*18);
    auto* Material=Cast<UMaterialInstanceDynamic>(Ride->Skirt->GetMaterial(3));FLinearColor Tint;
    TestTrue(TEXT("Per-slice tint applied"),Material && Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Tint")),Tint) && Tint==FLinearColor::Green);
    Ride->FigureActorClass=AStaticMeshActor::StaticClass();Ride->SeatActorClass=AStaticMeshActor::StaticClass();Ride->RebuildRide();
    TestTrue(TEXT("Figure class can be replaced"),Ride->Figure->GetChildActor()->IsA<AStaticMeshActor>());
    TestEqual(TEXT("Replacement seats survive rebuild"),Ride->GetGeneratedSeatCount(),12);
    Ride->bShowSeamLights=false;Ride->bShowSeats=false;Ride->bShowFigure=false;Ride->bEnableCollision=false;Ride->RebuildRide();
    TestEqual(TEXT("Lights fully disabled"),Ride->GetGeneratedBulbCount(),0);
    TestEqual(TEXT("Seats fully disabled"),Ride->GetGeneratedSeatCount(),0);
    TestNull(TEXT("Figure disabled"),Ride->Figure->GetChildActor());
    TestEqual(TEXT("Deck collision disabled"),Ride->Deck->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    TestEqual(TEXT("Skirt collision disabled"),Ride->Skirt->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    return true;
}
#endif
