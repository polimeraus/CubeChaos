#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricPlatform.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricPlatformOrganicShapeTest,"MyProject6.Platform.ParametricShape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricPlatformOrganicShapeTest::RunTest(const FString& Parameters)
{
    using namespace UE::Geometry;
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* P=World->SpawnActor<AParametricPlatform>();
    P->OuterShape=EChunkedDecorativeFloorShape::ParametricShape;
    P->BorderStyle=EPlatformBorder::None;
    P->TileSize=FVector2D(300);
    P->FillRule=EChunkedFloorFillRule::StretchCells;
    P->bUseDestructionRipple=false; P->FlashDuration=0;
    auto Check=[&](const FString& Name,bool Collision=true)
    {
        if (!TestTrue(Name+TEXT(" valid outline"),P->CachedShapeOutline.Num()>=3)) return;
        const auto Polygon=P->GetPlatformPolygon();
        double Area=0;
        bool Valid=true,Closed=true,PaintFits=true;
        for (const auto& Component : P->ChunkComponents)
        {
            const FDynamicMesh3& Mesh=*Component->GetMesh();
            Valid &= Mesh.CheckValidity();
            if (P->PlatformMode==EPlatformMode::Solid && P->FloorPattern==EChunkedFloorPattern::Solid) Closed &= Mesh.IsClosed();
            for (int32 ID : Mesh.TriangleIndicesItr())
            {
                FVector3d A,B,C; Mesh.GetTriVertices(ID,A,B,C);
                if (FMath::IsNearlyZero(A.Z) && FMath::IsNearlyZero(B.Z) && FMath::IsNearlyZero(C.Z))
                    Area+=FVector3d::CrossProduct(B-A,C-A).Length()*.5;
                if (A.Z>=0 && B.Z>=0 && C.Z>=0)
                {
                    const FVector3d Center=(A+B+C)/3;
                    const FVector2D XY(Center.X,Center.Y);
                    int Hole,Edge; double T;
                    PaintFits &= Polygon.Contains(XY) || Polygon.DistanceSquared(XY,Hole,Edge,T)<.01;
                }
            }
        }
        TestTrue(Name+TEXT(" valid closed slab"),Valid && Closed);
        TestTrue(Name+TEXT(" area matches silhouette"),FMath::Abs(Area-Polygon.SignedArea())<Polygon.SignedArea()*.001+2);
        TestTrue(Name+TEXT(" painted motifs remain on platform"),PaintFits);
        TestTrue(Name+TEXT(" central circle stays at actor origin"),Polygon.Contains(FVector2D::ZeroVector));
        for (auto V : P->CachedShapeOutline)
            if (!FMath::IsFinite(V.X) || !FMath::IsFinite(V.Y) || FMath::Abs(V.X)>P->ShapeSize.X*.5+.1 || FMath::Abs(V.Y)>P->ShapeSize.Y*.5+.1)
            { AddError(Name+TEXT(" has non-finite or out-of-bounds geometry")); break; }
        if (!Collision) return;
        int32 Wrong=0;
        for (int32 Y=0; Y<19; ++Y) for (int32 X=0; X<19; ++X)
        {
            const FVector2D XY=P->ShapeSize*FVector2D((X+.231)/19-.5,(Y+.617)/19-.5);
            int Hole,Edge; double T;
            if (Polygon.DistanceSquared(XY,Hole,Edge,T)<4) continue;
            FHitResult Hit;
            const bool Hits=World->LineTraceSingleByChannel(Hit,FVector(XY.X,XY.Y,200),FVector(XY.X,XY.Y,-200),ECC_GameTraceChannel1);
            Wrong+=Hits!=Polygon.Contains(XY);
        }
        TestEqual(Name+TEXT(" collision follows curved boundary"),Wrong,0);
    };
    for (const auto Mode : {EPlatformMode::Solid,EPlatformMode::Cellular})
    for (int32 Count : {0,1,2,3,5,8,16})
    {
        P->ParametricShapeSettings=FPlatformParametricShape();
        auto& S=P->ParametricShapeSettings;
        S.ArmCount=Count;
        if (Count==2) S.SpreadDegrees=110;
        if (Count==5) { S.bMirrorBends=false; S.DefaultArm.Bend=.5; S.DefaultArm.SCurve=.22; }
        P->PlatformMode=Mode; P->RebuildPlatform();
        TestTrue(TEXT("Supported layout has no warnings"),P->ShapeWarning.IsEmpty());
        Check(FString::Printf(TEXT("%d arms mode %d"),Count,int32(Mode)));
        if (Mode==EPlatformMode::Cellular)
        {
            FHitResult Hit;
            TestTrue(TEXT("Hub collision"),World->LineTraceSingleByChannel(Hit,FVector(.231,.617,200),FVector(.231,.617,-200),ECC_GameTraceChannel1));
            TestTrue(TEXT("Arm layout remains destructible"),P->DamageCellFromHit(Hit,10000));
            FHitResult After;
            TestFalse(TEXT("Damage removes collision"),World->LineTraceSingleByChannel(After,FVector(.231,.617,200),FVector(.231,.617,-200),ECC_GameTraceChannel1));
            P->RestoreAllCells();
        }
    }
    P->PlatformMode=EPlatformMode::Solid;
    auto& S=P->ParametricShapeSettings;
    S=FPlatformParametricShape(); S.ArmCount=1; S.RotationDegrees=0; S.CenterRadius=400; S.JunctionBlend=0;
    S.DefaultArm.Length=1600; S.DefaultArm.EndRadius=300; S.DefaultArm.RootWidth=500;
    S.DefaultArm.NeckWidth=200; S.DefaultArm.EndWidth=400; S.DefaultArm.Bend=0;
    P->RebuildPlatform();
    TestTrue(TEXT("Center radius is physical cm"),P->IsPointInsideOuterShape({-398,0}) && !P->IsPointInsideOuterShape({-403,0}));
    TestTrue(TEXT("End platform is circular and physically sized"),P->IsPointInsideOuterShape({2000,295}) && !P->IsPointInsideOuterShape({2000,305}));
    const double NeckX=140+(2000-140)*S.DefaultArm.NeckPosition;
    TestTrue(TEXT("Arm narrows to configured neck"),P->IsPointInsideOuterShape({NeckX,95}) && !P->IsPointInsideOuterShape({NeckX,125}));
    const double WidthBefore=P->ShapeSize.X;
    S.DefaultArm.Length+=400; P->RebuildPlatform();
    TestTrue(TEXT("Length changes actual extent rather than being normalized away"),FMath::IsNearlyEqual(P->ShapeSize.X-WidthBefore,800.,5.));
    const auto Unscaled=P->CachedShapeOutline;
    S.UniformScale=2; P->RebuildPlatform();
    bool ScalesExactly=Unscaled.Num()==P->CachedShapeOutline.Num();
    for (int32 I=0; I<Unscaled.Num() && ScalesExactly; ++I) ScalesExactly &= (Unscaled[I]*2).Equals(P->CachedShapeOutline[I],.01);
    TestTrue(TEXT("Uniform scale preserves circular proportions"),ScalesExactly);
    S=FPlatformParametricShape();
    FPlatformParametricArmOverride Arm;
    Arm.ArmIndex=1; Arm.AngleOffset=15; Arm.Shape.Length=2700; Arm.Shape.EndRadius=900;
    Arm.Shape.Bend=-.5; Arm.Shape.SCurve=.3; Arm.Shape.EndOffset=400;
    S.ArmOverrides.Add(Arm);
    Arm.ArmIndex=2; Arm.bEnabled=false; S.ArmOverrides.Add(Arm);
    P->RebuildPlatform(); Check(TEXT("Asymmetric per-arm settings"));
    const auto SavedOutline=P->CachedShapeOutline;
    P->RebuildPlatform(); TestEqual(TEXT("Deterministic vertex count"),P->CachedShapeOutline.Num(),SavedOutline.Num());
    TestTrue(TEXT("Deterministic geometry"),P->CachedShapeOutline==SavedOutline);
    auto* Copy=World->SpawnActor<AParametricPlatform>(FVector(20000,0,0),FRotator::ZeroRotator);
    TestTrue(TEXT("Copies all authored controls"),Copy->CopyPlatformSettingsFrom(P));
    TestEqual(TEXT("Copy retains per-arm settings"),Copy->ParametricShapeSettings.ArmOverrides[0].Shape.Length,2700.f);
    TestTrue(TEXT("Copy keeps its own transform"),Copy->GetActorLocation().Equals(FVector(20000,0,0)));
    World->DestroyActor(Copy);
    for (auto Mode : {EPlatformMode::Solid,EPlatformMode::Cellular})
    for (auto Pattern : {EChunkedFloorPattern::Checkerboard,EChunkedFloorPattern::CirclesAndPaths,EChunkedFloorPattern::HeartMotif})
    {
        S=FPlatformParametricShape(); S.ArmCount=3;
        P->FloorPattern=Pattern; P->BorderStyle=EPlatformBorder::InsetBand; P->BoundaryInset=40; P->BorderWidth=25;
        P->PlatformMode=Mode; P->RebuildPlatform(); Check(TEXT("Pattern and edge compatibility"));
    }
    P->BorderStyle=EPlatformBorder::None; P->FloorPattern=EChunkedFloorPattern::Solid;
    P->PlatformMode=EPlatformMode::Solid;
    S=FPlatformParametricShape(); S.ArmOverrides.Add(Arm); S.ArmOverrides.Add(Arm);
    P->RebuildPlatform(); TestTrue(TEXT("Duplicate indices are explained"),P->ShapeWarning.Contains(TEXT("Duplicate")));
    S=FPlatformParametricShape(); S.CenterRadius=std::numeric_limits<float>::quiet_NaN(); S.ArmCount=200;
    S.DefaultArm.Bend=std::numeric_limits<float>::infinity(); S.UniformScale=-3;
    P->RebuildPlatform(); TestTrue(TEXT("Invalid script inputs are limited safely"),!P->ShapeWarning.IsEmpty());
    Check(TEXT("Sanitized inputs"),false);
    P->OuterShape=EChunkedDecorativeFloorShape::Grid; P->ShapeSize=FVector2D(900,700); P->RebuildPlatform();
    TestEqual(TEXT("Old shapes remain available"),P->GetPlatformHoleCount(),0);
    TestEqual(TEXT("Old shape sizing is unchanged"),P->ShapeSize,FVector2D(900,700));
    return true;
}
#endif
