// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/Tasks/BTTask_BossActivateAbilityByTag.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"

UBTTask_BossActivateAbilityByTag::UBTTask_BossActivateAbilityByTag()
{
	NodeName = TEXT("Boss Activate Ability By Tag");
}

EBTNodeResult::Type UBTTask_BossActivateAbilityByTag::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;

	if (!BB || !Boss || !Boss->GetAbilitySystemComponent())
	{
		return EBTNodeResult::Failed;
	}

	const FName TagName = BB->GetValueAsName(AttackTagKey);
	const FGameplayTag AbilityTag = FGameplayTag::RequestGameplayTag(TagName, false);
	if (!AbilityTag.IsValid())
	{
		return EBTNodeResult::Failed;
	}

	FGameplayTagContainer Tags;
	Tags.AddTag(AbilityTag);

	const bool bActivated = Boss->GetAbilitySystemComponent()->TryActivateAbilitiesByTag(Tags, true);
	return bActivated ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
