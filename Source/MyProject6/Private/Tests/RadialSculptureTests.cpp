#if WITH_DEV_AUTOMATION_TESTS
#include "DestructibleRadialSculpture.h"
#include "BallProjectile.h"
#include "CannonballProjectile.h"
#include "BFGProjectile.h"
#include "SculptureMeshComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadialSculptureTest,"MyProject6.Destructible.RadialSculpture",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRadialSculptureTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* A=World->SpawnActor<ADestructibleRadialSculpture>();
    if (!TestNotNull(TEXT("Native sculpture spawns"),A)) return false;
    TestEqual(TEXT("Keep the native root name used by saved placements"),A->GetRootComponent()->GetFName(),FName(TEXT("SculptureRoot")));
    TestTrue(TEXT("Shared chunk root follows the preserved sculpture transform"),A->FloorRoot->GetAttachParent()==A->GetRootComponent());
    TestTrue(TEXT("Uses the same destruction superclass as other structures"),A->IsA<AChunkedDestructibleFloor>());
    TestTrue(TEXT("Shared brief flash and explosion ripple are enabled"),A->FlashDuration==.08f && A->bUseDestructionRipple);
    A->FlashDuration=0;A->bPlayDestroyTickSound=false;A->bUseDestructionRipple=false;
    const int32 Count=A->GeneratedPieceCount;
    TestTrue(TEXT("Denser default increases the previous 1627 small cells"),A->PieceSize==40.f && Count>1627 && Count<5000);
    TestTrue(TEXT("Rendering is batched instead of one component per small cell"),A->ChunkComponents.Num()<100);
    TestFalse(TEXT("No idle tick"),A->IsActorTickEnabled());
    int32 FinCells=0,SphereCells=0,BaseCells=0;
    for (const auto& P:A->Pieces)
    {
        FinCells+=P.Part==ADestructibleRadialSculpture::EPart::Fin;
        SphereCells+=P.Part==ADestructibleRadialSculpture::EPart::Sphere;
        BaseCells+=P.Part==ADestructibleRadialSculpture::EPart::Base;
        if (!TestTrue(TEXT("Every cell is small in ALL world axes, not just height"),P.Bounds.GetSize().GetMax()<A->PieceSize*1.6)) return false;
        for (int32 T:P.Mesh.TriangleIndicesItr())
        {
            if (!TestTrue(TEXT("No degenerate fragment triangles"),P.Mesh.GetTriArea(T)>1.e-8)) return false;
            const auto* N=P.Mesh.Attributes()->PrimaryNormals();const auto I=N->GetTriangle(T);
            const FVector3d Smooth=FVector3d(N->GetElement(I.A)+N->GetElement(I.B)+N->GetElement(I.C)).GetSafeNormal();
            if (!TestTrue(TEXT("Clockwise front faces match the outward normals"),P.Mesh.GetTriNormal(T).Dot(Smooth)>.85)) return false;
        }
    }
    TestTrue(TEXT("Fins are subdivided horizontally and vertically"),FinCells>500);
    TestTrue(TEXT("Sphere wall remains split into small fragments"),SphereCells>100);
    TestTrue(TEXT("Square base also uses small tiles"),BaseCells>200);
    for (const auto& C:A->ChunkComponents) TestFalse(TEXT("No fragment simulates rigid-body physics"),C->IsSimulatingPhysics());
    for (double Boundary=3+2*A->PieceSize;Boundary<450;Boundary+=A->PieceSize)
    {
        const double Z=8+480*(1-FMath::Sqrt((Boundary-55)/395))-1;
        FHitResult SeamHit;
        TestTrue(TEXT("Adjacent clipped cells meet at the curved fin edge without cracks"),
            World->LineTraceSingleByChannel(SeamHit,FVector(Boundary-.1,-10,Z),FVector(Boundary-.1,10,Z),ECC_Visibility));
    }
    const FVector Start(330,-45,38),End(330,45,38);
    FHitResult Hit;FIntPoint Cell;
    if (!TestTrue(TEXT("Thin fin collision is present"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility))) return false;
    if (!TestTrue(TEXT("A hit resolves a small cell within its chunk"),A->ResolveExistingCellFromHit(Hit,Cell))) return false;
    UGameplayStatics::ApplyPointDamage(A,10,FVector::YAxisVector,Hit,nullptr,nullptr,nullptr);
    TestEqual(TEXT("Sublethal damage is isolated to the struck cell"),A->Cells[Cell.X].Health,15.f);
    auto* Ball=World->SpawnActor<ABallProjectile>(FVector(2000,0,2000),FRotator::ZeroRotator);
    static_cast<AActor*>(Ball)->NotifyHit(nullptr,A,Hit.GetComponent(),false,Hit.ImpactPoint,Hit.ImpactNormal,FVector::ZeroVector,Hit);
    TestEqual(TEXT("Existing yellow projectile removes only one small cell"),A->RemainingPieceCount,Count-1);
    TestTrue(TEXT("Projectile consumed"),Ball->IsActorBeingDestroyed());
    TestFalse(TEXT("Destroyed cell's collision disappears immediately"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    int32 CachedUpdates = 0;
    for (const auto& Chunk : A->ChunkComponents)
        if (const auto* Cached = Cast<USculptureMeshComponent>(Chunk)) CachedUpdates += Cached->GetCachedCollisionUpdates();
    TestTrue(TEXT("Destruction reuses cooked convex hulls while removing collision synchronously"),CachedUpdates > 0);
    TestTrue(TEXT("Adjacent portion of the same former wide strip remains intact"),
        World->LineTraceSingleByChannel(Hit,Start-FVector(80,0,0),End-FVector(80,0,0),ECC_Visibility));
    const FVector Sphere=A->GetSphereLocalCenter();
    TestEqual(TEXT("Default shell has 15 cm walls and a 100 cm inner radius"),A->GetSphereInnerRadius(),100.f);
    TestTrue(TEXT("Named interior anchor follows the sphere center"),A->GetSphereWorldCenter().Equals(Sphere,.001));
    TestTrue(TEXT("Usable interior accounts for polygonal collision clearance"),A->GetSphereInteriorWorldRadius()>98.f && A->GetSphereInteriorWorldRadius()<100.f);
    TestFalse(TEXT("Empty center is not a destructible cell"),A->WorldLocationToCell(Sphere,Cell));
    TestFalse(TEXT("The cavity contains no solid collision"),World->OverlapBlockingTestByChannel(Sphere,FQuat::Identity,
        ECC_Pawn,FCollisionShape::MakeSphere(A->GetSphereInteriorWorldRadius())));
    for (FVector Dir:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1),FVector(1,1,1)})
    {
        Dir.Normalize();FHitResult InteriorHit;
        TestFalse(TEXT("Rays traverse the open cavity freely"),World->LineTraceSingleByChannel(InteriorHit,Sphere,Sphere+Dir*98,ECC_Visibility));
        if (TestTrue(TEXT("Inner wall blocks rays from inside"),World->LineTraceSingleByChannel(InteriorHit,Sphere,Sphere+Dir*140,ECC_Visibility)))
        {
            TestTrue(TEXT("Inner surface is at the wall thickness, not the sphere center"),InteriorHit.Distance>98 && InteriorHit.Distance<101);
            TestTrue(TEXT("Inner surface has inward-facing collision normals"),FVector::DotProduct(InteriorHit.ImpactNormal,Dir)<-.9);
            TestTrue(TEXT("Inner impacts resolve a destructible shell cell"),A->ResolveExistingCellFromHit(InteriorHit,Cell));
        }
    }
    FTransform SpawnTransform;
    TestTrue(TEXT("A normal-sized actor can be placed in the center"),A->TryGetSphereSpawnTransform(SpawnTransform,25));
    TestTrue(TEXT("Spawn transform points to the cavity and preserves actor size"),SpawnTransform.GetLocation().Equals(Sphere)
        && SpawnTransform.GetScale3D().Equals(FVector::OneVector));
    auto* Contents=World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),SpawnTransform);
    Contents->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Contents->SetActorScale3D(FVector(.25));
    Contents->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    TestFalse(TEXT("Spawn query rejects overlap with an existing occupant"),A->TryGetSphereSpawnTransform(SpawnTransform,25));
    Contents->Destroy();
    TestFalse(TEXT("Oversized contents are rejected"),A->TryGetSphereSpawnTransform(SpawnTransform,110));
    TestFalse(TEXT("Offset content cannot extend through the wall"),A->TryGetSphereSpawnTransform(SpawnTransform,25,FVector(90,0,0)));
    TestFalse(TEXT("Zero-radius query is rejected"),A->TryGetSphereSpawnTransform(SpawnTransform,0));
    if (!TestTrue(TEXT("Outer sphere wall blocks incoming fire"),World->LineTraceSingleByChannel(Hit,Sphere+FVector(300,40,40),Sphere+FVector(0,40,40),ECC_Visibility))) return false;
    UGameplayStatics::ApplyPointDamage(A,25,-FVector::XAxisVector,Hit,nullptr,nullptr,nullptr);
    TestEqual(TEXT("Sphere shot removes one small fragment"),A->RemainingPieceCount,Count-2);
    TestFalse(TEXT("Broken shell cell opens a passage from inside to outside"),World->LineTraceSingleByChannel(Hit,
        Sphere+FVector(0,40,40),Sphere+FVector(300,40,40),ECC_Visibility));
    TestTrue(TEXT("Another point in the old octant remains solid"),World->LineTraceSingleByChannel(Hit,
        Sphere+FVector(300,80,10),Sphere+FVector(0,80,10),ECC_Visibility));
    const double Angle=PI/16.;const FVector Gap(330*FMath::Cos(Angle),330*FMath::Sin(Angle),38);
    TestFalse(TEXT("No invisible cone closes the spaces between fins"),World->LineTraceSingleByChannel(Hit,Gap,Gap+FVector(0,0,150),ECC_Visibility));
    TestTrue(TEXT("Localized explosion removes many small cells"),A->ApplyExplosion(FVector(170,-170,260),130,100)>5);
    TestTrue(TEXT("Explosion leaves most of the sculpture intact"),A->RemainingPieceCount>Count/2);
    A->RebuildSculpture();
    TestTrue(TEXT("Repair restores original collision"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    auto* Bomb=World->SpawnActor<ACannonballProjectile>(FVector(2000,0,2000),FRotator::ZeroRotator);
    Bomb->SetDestructionDiameter(250);
    static_cast<AActor*>(Bomb)->NotifyHit(nullptr,A,Hit.GetComponent(),false,Hit.ImpactPoint,Hit.ImpactNormal,FVector::ZeroVector,Hit);
    TestTrue(TEXT("Red cannonball uses common floor explosion path"),A->RemainingPieceCount<Count-2);
    A->RebuildSculpture();World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility);
    auto* BFG=World->SpawnActor<ABFGProjectile>(FVector(2000,0,2000),FRotator::ZeroRotator);BFG->Explode(Hit);
    TestTrue(TEXT("BFG uses common floor explosion path"),A->RemainingPieceCount<Count-5);
    // Match the saved level's 5.75x sculpture scale, testing both supporting fins
    // and the hollow sphere through the real projectile/shared ripple path.
    A->SetActorScale3D(FVector(5.75));
    A->bUseDestructionRipple=true;
    for (bool bSphere : {false,true})
    {
        int32 Removed[3]={};
        for (int32 VariantIndex=0;VariantIndex<3;++VariantIndex)
        {
            A->RebuildSculpture();
            const FVector LocalStart=bSphere?Sphere+FVector(300,40,40):Start;
            const FVector LocalEnd=bSphere?Sphere+FVector(0,40,40):End;
            if (!TestTrue(TEXT("Scaled sculpture has a hittable surface"),World->LineTraceSingleByChannel(Hit,
                A->GetActorTransform().TransformPosition(LocalStart),A->GetActorTransform().TransformPosition(LocalEnd),ECC_GameTraceChannel1)))return false;
            auto* ComparisonShot=World->SpawnActor<ABFGProjectile>(FVector(10000,0,10000),FRotator::ZeroRotator);
            if (VariantIndex==0) { ComparisonShot->SculptureRadiusMultiplier=1;ComparisonShot->SculptureDamageMultiplier=1; }
            if (VariantIndex==2) { ComparisonShot->SculptureRadiusMultiplier=3;ComparisonShot->SculptureDamageMultiplier=2; }
            ComparisonShot->Explode(Hit);
            A->ProcessQueuedDestructions(World->GetTimeSeconds()+10.);
            Removed[VariantIndex]=Count-A->RemainingPieceCount;
            ComparisonShot->Destroy();
        }
        AddInfo(FString::Printf(TEXT("BFG %s at level scale: original %d cells, balanced %d cells, previous strong setting %d cells"),bSphere?TEXT("sphere"):TEXT("fin"),Removed[0],Removed[1],Removed[2]));
        TestTrue(TEXT("Balanced BFG destroys more cells than the original and fewer than the previous strong setting"),
            Removed[0]>0 && Removed[1]>Removed[0] && Removed[1]<Removed[2]);
        TestTrue(TEXT("A single BFG hit leaves some of the large sculpture intact"),Removed[1]<Count);
    }
    A->bUseDestructionRipple=false;
    A->RebuildSculpture();
    A->SetActorTransform(FTransform(FRotator(0,37,0),FVector(1600,-900,75),FVector(1.3,.8,1.6)));
    TestTrue(TEXT("Interior anchor follows transformed and scaled sculpture"),A->GetSphereWorldCenter().Equals(A->GetActorTransform().TransformPosition(Sphere),.001));
    TestTrue(TEXT("Spawn query respects nonuniform scale and local orientation"),A->TryGetSphereSpawnTransform(SpawnTransform,20,FVector(10,0,0),FRotator(0,15,0)));
    TestTrue(TEXT("Spawn position follows local offset but spawned actor keeps normal scale"),
        SpawnTransform.GetLocation().Equals(A->GetActorTransform().TransformPosition(Sphere+FVector(10,0,0)),.001)
        && SpawnTransform.GetRotation().Rotator().Equals(FRotator(0,52,0),.001) && SpawnTransform.GetScale3D().Equals(FVector::OneVector));
    TestTrue(TEXT("World clearance uses the smallest scale axis"),A->GetSphereInteriorWorldRadius()>78 && A->GetSphereInteriorWorldRadius()<80);
    TestTrue(TEXT("Scaled rotated actor still resolves exact cells"),World->LineTraceSingleByChannel(Hit,
        A->GetActorTransform().TransformPosition(Start),A->GetActorTransform().TransformPosition(End),ECC_Visibility) && A->ResolveExistingCellFromHit(Hit,Cell));
    UGameplayStatics::ApplyPointDamage(A,25,FVector::YAxisVector,Hit,nullptr,nullptr,nullptr);
    TestEqual(TEXT("Transformed hit still removes only one cell"),A->RemainingPieceCount,Count-1);
    A->SetCanBeDamaged(false);A->ShatterSculpture(FVector::UpVector);
    TestEqual(TEXT("Disabled damage is honored"),A->RemainingPieceCount,Count-1);
    A->SetCanBeDamaged(true);A->ShatterSculpture(FVector::UpVector);
    TestEqual(TEXT("Full destruction removes every cell without leaving fallen slabs"),A->RemainingPieceCount,0);
    for (const auto& C:A->ChunkComponents)
    {
        TestFalse(TEXT("No physics bodies even after total destruction"),C->IsSimulatingPhysics());
        TestEqual(TEXT("No persistent debris geometry"),C->GetDynamicMesh()->GetTriangleCount(),0);
    }
    A->RebuildSculpture();TestEqual(TEXT("Repair cancels scheduled removal"),A->GetLifeSpan(),0.f);
    A->bUseDestructionRipple=true;
    const int32 Affected=A->ApplyExplosion(A->GetActorTransform().TransformPosition(FVector(170,-170,260)),170,100);
    TestTrue(TEXT("Shared ripple queues a localized destruction batch"),Affected>1 && A->PendingCellIndices.Num()>0);
    A->ProcessQueuedDestructions(World->GetTimeSeconds()+10.);
    TestTrue(TEXT("Shared ripple completes and empties its queue"),A->RemainingPieceCount<Count && A->PendingCellIndices.IsEmpty());
    A->FinCount=8;A->bIncludeBase=false;A->RebuildSculpture();
    const int32 Variant=A->GeneratedPieceCount;
    TestTrue(TEXT("Parameter changes regenerate a smaller structure"),Variant<Count && Variant>SphereCells);
    A->PieceSize=35;A->RebuildSculpture();
    TestTrue(TEXT("Piece Size controls destruction resolution"),A->GeneratedPieceCount>Variant);
    A->SetActorTransform(FTransform::Identity);
    for (float Thickness:{1.f,50.f,1000.f})
    {
        A->SphereWallThickness=Thickness;A->RebuildSculpture();
        const float ExpectedInner=A->SphereRadius-FMath::Clamp(Thickness,1.f,A->SphereRadius*.9f);
        TestEqual(TEXT("Wall thickness parameter controls inner radius with a hollow upper bound"),A->GetSphereInnerRadius(),ExpectedInner);
        TestFalse(TEXT("Even thin shells leave their entire safe cavity empty of supporting fins"),World->OverlapBlockingTestByChannel(
            A->GetSphereWorldCenter(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(A->GetSphereInteriorWorldRadius())));
    }
    A->SphereWallThickness=15;A->RebuildSculpture();
    A->MaxGeneratedCells=1;A->RebuildSculpture();
    TestTrue(TEXT("Budget failure preserves geometry and reports the limit"),!A->GenerationWarning.IsEmpty() && A->GeneratedPieceCount>Variant);
    return true;
}
#endif
