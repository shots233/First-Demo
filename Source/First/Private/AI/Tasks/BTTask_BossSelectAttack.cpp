// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/Tasks/BTTask_BossSelectAttack.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"

UBTTask_BossSelectAttack::UBTTask_BossSelectAttack()
{
	NodeName = TEXT("Boss Select Attack");
}

EBTNodeResult::Type UBTTask_BossSelectAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;
	AActor* Target = BB
		? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;

	if (!BB || !Boss || !Target || !Boss->GetAbilitySystemComponent())
	{
		return EBTNodeResult::Failed;
	}

	const float Distance = FVector::Dist(
		Boss->GetActorLocation(),
		Target->GetActorLocation());

	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();

	// 收集可用招式：距离、冷却与后撤落点都满足要求，并累计随机权重。
	TArray<int32> EligibleIndexes;
	float TotalWeight = 0.f;
	for (int32 i = 0; i < AttackOptions.Num(); ++i)
	{
		const FBossAttackOption& Option = AttackOptions[i];
		if (!Option.AbilityTag.IsValid() ||
			!FMath::IsFinite(Option.MinRange) ||
			!FMath::IsFinite(Option.MaxRange) ||
			!FMath::IsFinite(Option.SelectionWeight) ||
			!FMath::IsFinite(Option.RequiredRetreatSpace) ||
			Option.SelectionWeight <= 0.f)
		{
			continue;
		}
		if (Option.CooldownTag.IsValid() && ASC->HasMatchingGameplayTag(Option.CooldownTag))
		{
			continue;
		}
		if (Distance < Option.MinRange || Distance > Option.MaxRange)
		{
			continue;
		}
		if (Option.RequiredRetreatSpace > 0.f &&
			!Boss->HasSafeRetreatSpace(Option.RequiredRetreatSpace))
		{
			continue;
		}
		EligibleIndexes.Add(i);
		TotalWeight += Option.SelectionWeight;
	}

	if (EligibleIndexes.IsEmpty() ||
		!FMath::IsFinite(TotalWeight) ||
		TotalWeight <= 0.f)
	{
		return EBTNodeResult::Failed;
	}

	float RemainingWeight = FMath::FRand() * TotalWeight;
	int32 SelectedIndex = EligibleIndexes.Last();
	for (const int32 EligibleIndex : EligibleIndexes)
	{
		RemainingWeight -= AttackOptions[EligibleIndex].SelectionWeight;
		if (RemainingWeight <= 0.f)
		{
			SelectedIndex = EligibleIndex;
			break;
		}
	}

	BB->SetValueAsName(AttackTagKey, AttackOptions[SelectedIndex].AbilityTag.GetTagName());

	return EBTNodeResult::Succeeded;
}
