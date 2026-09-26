#include "AI/Decorators/BTDecorator_BossCanPursue.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "MyGameplayTags.h"

UBTDecorator_BossCanPursue::UBTDecorator_BossCanPursue()
{
	NodeName = TEXT("Boss Can Pursue");
	bCreateNodeInstance = true;
	bNotifyTick = bNotifyBecomeRelevant = true;
	FlowAbortMode = EBTFlowAbortMode::LowerPriority;
	bAllowAbortNone = false;
	bAllowAbortLowerPri = true;
	bAllowAbortChildNodes = false;
	AbilityTag = MyGameplayTags::Boss_Ability_Attack_Pursuit;
	TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(ThisClass, TargetActorKey), AActor::StaticClass());
	TargetActorKey.SelectedKeyName = TEXT("TargetActor");
	ShouldChaseKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(ThisClass, ShouldChaseKey));
	ShouldChaseKey.SelectedKeyName = TEXT("bShouldChase");
	PlayerTooFarKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(ThisClass, PlayerTooFarKey));
	PlayerTooFarKey.SelectedKeyName = TEXT("bPlayerTooFar");
	BusyKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(ThisClass, BusyKey));
	BusyKey.SelectedKeyName = TEXT("bIsBusy");
	ProvokedKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(ThisClass, ProvokedKey));
	ProvokedKey.SelectedKeyName = TEXT("bProvokedByDamage");
}

void UBTDecorator_BossCanPursue::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (UBlackboardData* Blackboard = GetBlackboardAsset())
	{
		TargetActorKey.ResolveSelectedKey(*Blackboard);
		ShouldChaseKey.ResolveSelectedKey(*Blackboard);
		PlayerTooFarKey.ResolveSelectedKey(*Blackboard);
		BusyKey.ResolveSelectedKey(*Blackboard);
		ProvokedKey.ResolveSelectedKey(*Blackboard);
	}
}

bool UBTDecorator_BossCanPursue::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	const AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Blackboard || !IsValid(Pawn) || !Pawn->HasAuthority() || !AbilityTag.IsValid() ||
		!FMath::IsFinite(MinTriggerRange) || !FMath::IsFinite(MaxTriggerRange) ||
		MinTriggerRange <= 0.f || MaxTriggerRange < MinTriggerRange ||
		TargetActorKey.GetSelectedKeyID() == FBlackboard::InvalidKey ||
		ShouldChaseKey.GetSelectedKeyID() == FBlackboard::InvalidKey ||
		PlayerTooFarKey.GetSelectedKeyID() == FBlackboard::InvalidKey ||
		BusyKey.GetSelectedKeyID() == FBlackboard::InvalidKey ||
		ProvokedKey.GetSelectedKeyID() == FBlackboard::InvalidKey) return false;
	if (!Blackboard->GetValueAsBool(ShouldChaseKey.SelectedKeyName) ||
		!Blackboard->GetValueAsBool(PlayerTooFarKey.SelectedKeyName) ||
		Blackboard->GetValueAsBool(BusyKey.SelectedKeyName) ||
		Blackboard->GetValueAsBool(ProvokedKey.SelectedKeyName)) return false;
	AActor* Target = Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName));
	if (!IsValid(Target) || Target == Pawn || Target->IsActorBeingDestroyed()) return false;
	const FVector Delta = Target->GetActorLocation() - Pawn->GetActorLocation();
	const float Distance = Delta.Size2D();
	if (Delta.ContainsNaN() || Distance < MinTriggerRange || Distance > MaxTriggerRange) return false;
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn);
	if (!ASC || !ASC->AbilityActorInfo.IsValid() || ASC->AbilityActorInfo->AvatarActor.Get() != Pawn) return false;
	// 一个标签对应一个已授予能力，避免配置重复时一次发动多个动作。
	const FGameplayAbilitySpec* Match = nullptr;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && !Spec.PendingRemove && Spec.Ability->GetAssetTags().HasTagExact(AbilityTag))
		{
			if (Match) return false;
			Match = &Spec;
		}
	}
	if (!Match || Match->IsActive()) return false;
	const UGameplayAbility* Ability = Match->GetPrimaryInstance() ? Match->GetPrimaryInstance() : Match->Ability.Get();
	FGameplayEventData Event;
	Event.EventTag = AbilityTag;
	Event.Instigator = Pawn;
	Event.Target = Target;
	return Ability->CanActivateAbility(Match->Handle, ASC->AbilityActorInfo.Get()) &&
		Ability->ShouldAbilityRespondToEvent(ASC->AbilityActorInfo.Get(), &Event);
}

void UBTDecorator_BossCanPursue::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);
	TimeUntilCheck = 0.f;
}

void UBTDecorator_BossCanPursue::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
	TimeUntilCheck -= DeltaSeconds;
	if (TimeUntilCheck > 0.f) return;
	TimeUntilCheck = 0.1f;
	// 同时观察距离/冷却变化，不依赖 Move To 自然结束或黑板布尔值变化。
	ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
}

FString UBTDecorator_BossCanPursue::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s\n%s: %.0f - %.0f cm\nChecks chase, target and ability every 0.1s. Aborts lower priority only."),
		*Super::GetStaticDescription(), *AbilityTag.ToString(), MinTriggerRange, MaxTriggerRange);
}
