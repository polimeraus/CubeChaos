#include "NPCHealthBarComponent.h"

#include "NPCHealthBarWidget.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

UNPCHealthBarComponent::UNPCHealthBarComponent()
{
	SetWidgetClass(UNPCHealthBarWidget::StaticClass());
	SetWidgetSpace(EWidgetSpace::Screen);
	SetDrawSize(FVector2D(64.0f, 7.0f));
	// Visibility must be rechecked while occluded so the bar can reappear.
	SetTickMode(ETickMode::Enabled);
	SetPivot(FVector2D(0.5f, 1.0f));
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetCastShadow(false);
}

void UNPCHealthBarComponent::BeginPlay()
{
	Super::BeginPlay();
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		USkeletalMeshComponent* Mesh = Character->GetMesh();
		// Ghoul and Whisper use head; RadicalMike uses head_01.
		for (const FName Candidate : { FName(TEXT("head")), FName(TEXT("head_01")) })
		{
			if (Mesh->DoesSocketExist(Candidate)) { HeadBone = Candidate; break; }
		}
		AddTickPrerequisiteComponent(Mesh);
		UpdateHeadPosition();
	}
	UpdateVisibility();
}

void UNPCHealthBarComponent::UpdateHeadPosition()
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		const FVector Position = HeadBone.IsNone()
			? Character->GetActorLocation() + FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 18.0f)
			: Character->GetMesh()->GetSocketLocation(HeadBone) + FVector(0, 0, FMath::Max(0.0f, HeadClearance));
		SetWorldLocation(Position);
	}
}

void UNPCHealthBarComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (bHasHealth) { UpdateHeadPosition(); }
	SetBarColor(BarColor);
	UpdateVisibility();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UNPCHealthBarComponent::UpdateHealth(float CurrentHealth, float MaxHealth)
{
	if (GetNetMode() == NM_DedicatedServer) { return; }
	bHasHealth = CurrentHealth > 0.0f;
	InitWidget();
	if (UNPCHealthBarWidget* Gauge = Cast<UNPCHealthBarWidget>(GetUserWidgetObject()))
	{
		Gauge->SetHealthPercent(CurrentHealth / FMath::Max(1.0f, MaxHealth));
		Gauge->SetBarColor(BarColor);
	}
	UpdateVisibility();
	SetComponentTickEnabled(bHasHealth);
}

void UNPCHealthBarComponent::SetBarColor(FLinearColor InColor)
{
	BarColor = InColor;
	if (UNPCHealthBarWidget* Gauge = Cast<UNPCHealthBarWidget>(GetUserWidgetObject()))
	{
		Gauge->SetBarColor(BarColor);
	}
}

bool UNPCHealthBarComponent::CanSeeCharacter() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || Character->IsHidden() || !Character->GetMesh()->IsVisible()
		|| Character->GetMesh()->bHiddenInGame) { return false; }
	const ULocalPlayer* LocalPlayer = GetOwnerPlayer();
	const APlayerController* Viewer = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld())
		: GetWorld()->GetFirstPlayerController();
	if (!Viewer || !Viewer->IsLocalController()) { return false; }

	FVector ViewLocation;
	FRotator ViewRotation;
	Viewer->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Head = HeadBone.IsNone() ? Character->GetActorLocation()
		: Character->GetMesh()->GetSocketLocation(HeadBone);
	if (FVector::DotProduct(Head - ViewLocation, ViewRotation.Vector()) <= 0.0) { return false; }

	FCollisionQueryParams Query(SCENE_QUERY_STAT(NPCHealthBarVisibility), true, Character);
	Query.AddIgnoredActor(Viewer->GetPawn());
	Query.AddIgnoredActor(Viewer->GetViewTarget());
	// Check the character itself, not the floating bar which can stick above a wall.
	// Head or torso visibility is enough for characters partially behind cover.
	const FVector Torso = (Head + Character->GetMesh()->GetComponentLocation()) * 0.5;
	for (const FVector& Target : { Head, Torso })
	{
		if (!GetWorld()->LineTraceTestByChannel(ViewLocation, Target, ECC_Visibility, Query)) { return true; }
	}
	return false;
}

void UNPCHealthBarComponent::UpdateVisibility()
{
	SetVisibility(bHasHealth && CanSeeCharacter());
}
