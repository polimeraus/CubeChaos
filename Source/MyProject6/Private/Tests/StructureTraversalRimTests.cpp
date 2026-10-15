#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ChunkedDestructibleConeStructure.h"
#include "QuadPanelConeStructure.h"
#include "Components/ChildActorComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStructureTraversalRimTest,"MyProject6.Destructible.StructureTraversalRim",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FStructureTraversalRimTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto Advance=[&](int32 N){for(int32 I=0;I<N;++I){++GFrameCounter;World->Tick(LEVELTICK_All,1.f/120);}};
    auto Check=[&]<typename T>(T* Cone)
    {
        AddInfo(Cone->GetClass()->GetName());
        Cone->ConeAnimationType=EConeStructureAnimationType::None;Cone->SetReactToMusic(false);
        Cone->bScatterDestroyedPieces=false;Cone->bPlayDestroyTickSound=false;Cone->FlashDuration=0;
        Cone->bUseAsyncConeCollisionCooking=false;Cone->bUseDestructionRipple=false;
        Cone->PlatformStyle=EConeStructurePlatformStyle::RisingSpiral;
        Cone->SpiralDestructionMode=EConeStructureSpiralDestructionMode::OneSegmentPerHit;
        Cone->LowestPlatformHeightBlocks=2;Cone->SpiralEndHeightBlocks=18;Cone->PlatformThicknessBlocks=1;
        Cone->PlatformOverhangBlocks=3;Cone->PlatformSpiralTurns=2;Cone->SpiralSegmentsPerTurn=48;
        Cone->CircularSegments=48;Cone->MaxRuntimeCircularSegments=48;
        Cone->SpiralRimHeight=35;Cone->SpiralRimWidth=18;
        Cone->ConfigureConeStructure(24,8,0,1,FVector(70),FVector::ZeroVector);
        Cone->FinishSpawning(FTransform::Identity);
        auto* Pad=Cast<ALaunchPad>(Cone->GroundLaunchPad->GetChildActor());
        if (!TestNotNull(TEXT("Cone owns ground launch pad"),Pad))return;
        FCollisionQueryParams Q;Q.AddIgnoredActor(Pad);FHitResult Hit;
        const double Height=Cone->GridHeightBlocks*70;
        TestEqual(TEXT("Cone tip keeps requested clear diameter"),Cone->EffectiveOpeningWidth,200.f);
        TestFalse(TEXT("Capsule clears cone interior and open tip"),World->SweepSingleByChannel(Hit,FVector(0,0,200),FVector(0,0,Height+200),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96),Q));
        auto* Character=World->SpawnActor<ACharacter>(FVector(2000,0,300),FRotator::ZeroRotator);
        Character->GetCapsuleComponent()->SetCapsuleSize(34,96);Character->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
        auto* Movement=Character->GetCharacterMovement();Movement->bRunPhysicsWithNoController=true;
        Movement->SetMovementMode(MOVE_Falling);
        Character->SetActorLocation(Pad->GetActorLocation()+FVector(20,0,106));Advance(10);
        TestTrue(TEXT("Cone pad launches the actual character"),Character->GetVelocity().Z>1000);
        double Peak=0;for(int32 I=0;I<300;++I){Advance(1);Peak=FMath::Max(Peak,Character->GetActorLocation().Z-96);}
        TestTrue(TEXT("Character exits above cone"),Peak>Height+200);Character->Destroy();
        for(bool Clockwise:{false,true})
        {
            Cone->bPlatformSpiralClockwise=Clockwise;Cone->bSpiralOuterRim=true;Cone->RebuildConeStructure(false);
            const int32 Segment=Cone->GetSpiralSegmentCount()/3;
            FVector Edge[2];
            for(int32 E=0;E<2;++E)
            {
                const float Alpha=float(Segment+E)/Cone->GetSpiralSegmentCount();
                const double Z=FMath::Lerp(Cone->GetSpiralBottomHeight(false),Cone->GetSpiralBottomHeight(true),Alpha)+Cone->GetSpiralThickness();
                const double Angle=FMath::DegreesToRadians(Cone->SpiralStartAngleDegrees)+(Clockwise?-1:1)*UE_TWO_PI*Cone->PlatformSpiralTurns*Alpha;
                Edge[E]=FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Cone->GetPlatformOuterRadiusAtHeight(Z)+FVector(0,0,Z);
            }
            const FVector Center=(Edge[0]+Edge[1])*.5+FVector(0,0,17),Radial=FVector(Center.X,Center.Y,0).GetSafeNormal();
            const FVector3d Sample=FVector3d(Center-Radial*9);
            for(int32 Axis=0;Axis<3;++Axis)
            {
                FVector3d U=FVector3d::Zero(),V=U,N=U;U[(Axis+1)%3]=1;V[(Axis+2)%3]=1;N[Axis]=1;
                UE::Geometry::FDynamicMesh3 Cut;Cut.EnableVertexNormals(FVector3f::UnitZ());Cut.EnableAttributes();
                Cut.Attributes()->SetNumUVLayers(1);Cut.Attributes()->EnableMaterialID();
                Cone->AppendClippedDestructionCutFace(Cut,Sample-U-V,Sample+U-V,Sample+U+V,Sample-U+V,FVector3f(N),1);
                double Area=0;for(int32 TID:Cut.TriangleIndicesItr())Area+=Cut.GetTriArea(TID);
                TestTrue(TEXT("Voxel cuts cover the raised rim volume exactly"),FMath::IsNearlyEqual(Area,4.,.001));
            }
            const FVector Start=Center+Radial*40,End=Center-Radial*30;
            if(TestTrue(TEXT("Raised rim has real blocking collision"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Q)))
            {
                TestTrue(TEXT("Rim hit destroys its platform segment"),Cone->DestroyConeCellFromHit(Hit));
                Cone->ProcessQueuedDestructions(1.e10);Cone->FlushDirtyChunks();
                TestFalse(TEXT("Destroyed rim collision is removed"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Q));
            }
            Cone->bSpiralOuterRim=false;Cone->RebuildConeStructure(false);
            TestFalse(TEXT("Rim toggle removes the raised geometry"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Q));
        }
        Cone->Traversal.bGroundLaunchPad=false;Cone->Traversal.bFloorOpenings=false;Cone->RebuildConeStructure(false);
        TestNull(TEXT("Cone pad can be disabled independently"),Cone->GroundLaunchPad->GetChildActor());
        TestTrue(TEXT("Closed pointed tip blocks capsule again"),World->SweepSingleByChannel(Hit,FVector(0,0,200),FVector(0,0,Height+200),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96)));
        Cone->Destroy();
    };
    FActorSpawnParameters P;P.bDeferConstruction=true;
    Check(World->SpawnActor<AChunkedDestructibleConeStructure>(AChunkedDestructibleConeStructure::StaticClass(),FTransform::Identity,P));
    Check(World->SpawnActor<AQuadPanelConeStructure>(AQuadPanelConeStructure::StaticClass(),FTransform::Identity,P));
    return true;
}
#endif
