#include "AI/Tasks/BTTask_ActivateAbilityAndWait.h"

#include "AIController.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "MyGameplayTags.h"

UBTTask_ActivateAbilityAndWait::UBTTask_ActivateAbilityAndWait()
{
	NodeName = TEXT("Activate Ability And Wait");
	bCreateNodeInstance = true;
	bIgnoreRestartSelf = true;
	INIT_TASK_NODE_NOTIFY_FLAGS();
	AbilityTag = MyGameplayTags::Boss_Ability_Attack_Pursuit;
	TargetActorKey.SelectedKeyName = TEXT("TargetActor");
	TargetActorKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_ActivateAbilityAndWait, TargetActorKey), AActor::StaticClass());
}

void UBTTask_ActivateAbilityAndWait::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* Blackboard = GetBlackboardAsset())
	{
		TargetActorKey.ResolveSelectedKey(*Blackboard);
	}
}

FString UBTTask_ActivateAbilityAndWait::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s\nAbility: %s\nTarget: %s\nWait for completion; cancel on abort or target change."),
		*Super::GetStaticDescription(), *AbilityTag.ToString(), *TargetActorKey.SelectedKeyName.ToString());
}

EBTNodeResult::Type UBTTask_ActivateAbilityAndWait::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// Activation may synchronously invoke gameplay callbacks which stop/restart a tree.
	// Never start a second execution of this node while the first activation is on the stack.
	if (bInsideExecuteTask)
	{
		return EBTNodeResult::Failed;
	}
	TGuardValue<bool> ExecutionGuard(bInsideExecuteTask, true);
	ClearExecution(true);

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* Target = Blackboard
		? Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName)) : nullptr;
	if (!IsValid(Pawn) || !Pawn->HasAuthority() || !IsValid(Target)
		|| Target->IsActorBeingDestroyed() || !AbilityTag.IsValid())
	{
		return EBTNodeResult::Failed;
	}

	UAbilitySystemComponent* AbilitySystem = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn);
	if (!IsValid(AbilitySystem) || !AbilitySystem->AbilityActorInfo.IsValid()
		|| AbilitySystem->AbilityActorInfo->AvatarActor.Get() != Pawn)
	{
		return EBTNodeResult::Failed;
	}

	FGameplayAbilitySpecHandle SelectedHandle;
	{
		FScopedAbilityListLock AbilityListLock(*AbilitySystem);
		for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
		{
			if (!Spec.Ability || Spec.PendingRemove || !Spec.Ability->GetAssetTags().HasTagExact(AbilityTag))
			{
				continue;
			}
			// A shared category tag or duplicate grant is ambiguous. Do not start several abilities,
			// and do not adopt an activation that belongs to another task or caller.
			if (SelectedHandle.IsValid() || Spec.IsActive())
			{
				return EBTNodeResult::Failed;
			}
			SelectedHandle = Spec.Handle;
		}
	}
	if (!SelectedHandle.IsValid())
	{
		return EBTNodeResult::Failed;
	}

	ActiveAbilitySystem = AbilitySystem;
	ActiveTree = &OwnerComp;
	ActiveTarget = Target;
	ActiveAbilityHandle = SelectedHandle;
	bOwnsAbility = true;
	AbilityEndedDelegateHandle = AbilitySystem->OnAbilityEnded.AddUObject(this,
		&UBTTask_ActivateAbilityAndWait::HandleAbilityEnded);
	const uint32 ThisExecution = ExecutionGeneration;

	FGameplayEventData Payload;
	Payload.EventTag = AbilityTag;
	Payload.Instigator = Pawn;
	Payload.Target = Target;
	const bool bActivated = AbilitySystem->TriggerAbilityFromGameplayEvent(SelectedHandle,
		AbilitySystem->AbilityActorInfo.Get(), AbilityTag, &Payload, *AbilitySystem);

	if (ExecutionGeneration != ThisExecution)
	{
		// A tree shutdown during activation already unbound the delegate. If activation
		// nevertheless completed afterwards, do not leave that newly started skill running.
		if (bActivated && IsValid(AbilitySystem))
		{
			const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(SelectedHandle);
			if (Spec && Spec->IsActive())
			{
				AbilitySystem->CancelAbilityHandle(SelectedHandle);
			}
		}
		return EBTNodeResult::Failed;
	}
	if (!bActivated)
	{
		ClearExecution(false);
		return EBTNodeResult::Failed;
	}

	// Even a synchronous EndAbility is delivered on our next tick. FinishLatentTask must
	// not run inside ExecuteTask or inside GAS's ability-ended multicast broadcast.
	return EBTNodeResult::InProgress;
}

void UBTTask_ActivateAbilityAndWait::TickTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	UAbilitySystemComponent* AbilitySystem = ActiveAbilitySystem.Get();
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* Target = ActiveTarget.Get();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (ActiveTree.Get() != &OwnerComp || !IsValid(AbilitySystem) || !IsValid(Pawn)
		|| !Pawn->HasAuthority() || !AbilitySystem->AbilityActorInfo.IsValid()
		|| AbilitySystem->AbilityActorInfo->AvatarActor.Get() != Pawn || !IsValid(Target)
		|| Target->IsActorBeingDestroyed() || !Blackboard
		|| Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName) != Target)
	{
		CompleteExecution(OwnerComp, EBTNodeResult::Failed, true);
		return;
	}
	if (bReceivedAbilityEnd)
	{
		CompleteExecution(OwnerComp,
			bAbilityWasCancelled ? EBTNodeResult::Failed : EBTNodeResult::Succeeded, false);
		return;
	}

	// A removed spec/ASC must not leave the behavior tree waiting indefinitely.
	const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(ActiveAbilityHandle);
	if (!Spec || !Spec->IsActive())
	{
		CompleteExecution(OwnerComp, EBTNodeResult::Failed, false);
	}
}

void UBTTask_ActivateAbilityAndWait::HandleAbilityEnded(const FAbilityEndedData& EndedData)
{
	if (!ActiveAbilityHandle.IsValid() || EndedData.AbilitySpecHandle != ActiveAbilityHandle
		|| bReceivedAbilityEnd)
	{
		return;
	}
	bReceivedAbilityEnd = true;
	bAbilityWasCancelled = EndedData.bWasCancelled;
	// A later activation of this same granted spec belongs to its new caller.
	bOwnsAbility = false;
}

void UBTTask_ActivateAbilityAndWait::ClearExecution(bool bCancelOwnedAbility)
{
	UAbilitySystemComponent* AbilitySystem = ActiveAbilitySystem.Get();
	const FGameplayAbilitySpecHandle HandleToCancel = ActiveAbilityHandle;
	const bool bShouldCancel = bCancelOwnedAbility && bOwnsAbility && HandleToCancel.IsValid();
	if (IsValid(AbilitySystem) && AbilityEndedDelegateHandle.IsValid())
	{
		AbilitySystem->OnAbilityEnded.Remove(AbilityEndedDelegateHandle);
	}

	// Reset before CancelAbilityHandle: cancellation can synchronously call back into AI.
	AbilityEndedDelegateHandle.Reset();
	ActiveAbilitySystem.Reset();
	ActiveTree.Reset();
	ActiveTarget.Reset();
	ActiveAbilityHandle = FGameplayAbilitySpecHandle();
	bOwnsAbility = false;
	bReceivedAbilityEnd = false;
	bAbilityWasCancelled = false;
	++ExecutionGeneration;
	if (bShouldCancel && IsValid(AbilitySystem))
	{
		const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(HandleToCancel);
		if (Spec && Spec->IsActive())
		{
			AbilitySystem->CancelAbilityHandle(HandleToCancel);
		}
	}
}

void UBTTask_ActivateAbilityAndWait::CompleteExecution(UBehaviorTreeComponent& OwnerComp,
	EBTNodeResult::Type Result, bool bCancelOwnedAbility)
{
	const uint32 ClearedGeneration = ExecutionGeneration + 1;
	ClearExecution(bCancelOwnedAbility);
	// CancelAbilityHandle can synchronously finish/restart the tree. Do not finish a
	// replacement execution which happened to reuse this same node instance.
	if (ExecutionGeneration == ClearedGeneration && OwnerComp.GetTaskStatus(this) == EBTTaskStatus::Active)
	{
		FinishLatentTask(OwnerComp, Result);
	}
}

EBTNodeResult::Type UBTTask_ActivateAbilityAndWait::AbortTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ClearExecution(true);
	return EBTNodeResult::Aborted;
}

void UBTTask_ActivateAbilityAndWait::OnTaskFinished(UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	ClearExecution(true);
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

void UBTTask_ActivateAbilityAndWait::OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp)
{
	ClearExecution(true);
	Super::OnInstanceDestroyed(OwnerComp);
}
