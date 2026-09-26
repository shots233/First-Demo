// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/Tasks/BTTask_BossSelectAttack.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossPursuitAbility.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "DataAssets/StartUpData/DataAsset_StartUpDataBase.h"
#include "MyGameplayTags.h"
#include "UObject/UnrealType.h"

namespace
{
void SyncAttackCooldowns(UAbilitySystemComponent& ASC, const TArray<FBossAttackOption>& Options)
{
	// 配置写到当前角色拥有的 Spec，不修改共享的 GA 默认对象或行为树节点。
	// 整表同步发生在选招前：未被抽中的五连斩，由其它招式接续时也应使用同一冷却。
	// 同一招式配置互相矛盾时回退默认值，避免依赖数组顺序。
	for (FGameplayAbilitySpec& Spec : ASC.GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.Ability->IsA<UFirstBossGameplayAbility>() ||
			Spec.Ability->IsA<UFirstBossPursuitAbility>() ||
			!Spec.Ability->GetAssetTags().HasTag(MyGameplayTags::Boss_Ability_Attack))
		{
			continue;
		}

		TOptional<float> Override;
		bool bConflicting = false;
		for (const FBossAttackOption& Option : Options)
		{
			// 只接受具体招式身份，不允许 Attack 父标签同时改写所有招式。
			if (!Option.AbilityTag.IsValid() || Option.AbilityTag == MyGameplayTags::Boss_Ability_Attack ||
				!Spec.Ability->GetAssetTags().HasTagExact(Option.AbilityTag))
			{
				continue;
			}
			const float Duration = FMath::IsFinite(Option.CooldownDuration) && Option.CooldownDuration >= 0.f
				? Option.CooldownDuration : -1.f;
			if (Override.IsSet() && Override.GetValue() != Duration)
			{
				bConflicting = true;
			}
			Override = Duration;
		}

		const FGameplayTag DurationTag = MyGameplayTags::Boss_SetByCaller_CooldownDuration;
		if (!bConflicting && Override.IsSet() && Override.GetValue() >= 0.f)
		{
			Spec.SetByCallerTagMagnitudes.Add(DurationTag, Override.GetValue());
		}
		else
		{
			// 回到 -1、移除招式或切换另一张选招表时，不遗留上一张表的覆盖值。
			Spec.SetByCallerTagMagnitudes.Remove(DurationTag);
		}
	}
}
}

UBTTask_BossSelectAttack::UBTTask_BossSelectAttack()
{
	NodeName = TEXT("Boss Select Attack");
#if WITH_EDITORONLY_DATA
	DefaultCooldownSource = TSoftObjectPtr<UDataAsset_StartUpDataBase>(FSoftObjectPath(
		TEXT("/Game/Enemy/Data/DA_BossStartUpData.DA_BossStartUpData")));
#endif
}

void UBTTask_BossSelectAttack::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	InitializeCooldownDefaults();
#endif
}

#if WITH_EDITOR
bool UBTTask_BossSelectAttack::InitializeCooldownDefaults(int32 ChangedAbilityIndex)
{
	UDataAsset_StartUpDataBase* Source = nullptr;
	bool bSourceLoaded = false;
	bool bChanged = false;
	for (int32 Index = 0; Index < AttackOptions.Num(); ++Index)
	{
		if (ChangedAbilityIndex != INDEX_NONE && Index != ChangedAbilityIndex)
		{
			continue;
		}
		FBossAttackOption& Option = AttackOptions[Index];
		if (Index != ChangedAbilityIndex && FMath::IsFinite(Option.CooldownDuration) && Option.CooldownDuration >= 0.f)
		{
			continue;
		}
		if (!Option.AbilityTag.IsValid())
		{
			bChanged |= Option.CooldownDuration != 0.f;
			Option.CooldownDuration = 0.f;
			continue;
		}
		if (!bSourceLoaded)
		{
			Source = DefaultCooldownSource.LoadSynchronous();
			bSourceLoaded = true;
		}
		const UFirstBossGameplayAbility* Match = nullptr;
		bool bAmbiguous = false;
		auto FindMatch = [&Option, &Match, &bAmbiguous](const TArray<TSubclassOf<UGameplayAbility>>& Classes)
		{
			for (const TSubclassOf<UGameplayAbility>& Class : Classes)
			{
				const UFirstBossGameplayAbility* Ability = Class ? Cast<UFirstBossGameplayAbility>(Class->GetDefaultObject()) : nullptr;
				if (Ability && !Ability->IsA<UFirstBossPursuitAbility>() &&
					Option.AbilityTag != MyGameplayTags::Boss_Ability_Attack &&
					Ability->GetAssetTags().HasTag(MyGameplayTags::Boss_Ability_Attack) &&
					Ability->GetAssetTags().HasTagExact(Option.AbilityTag))
				{
					bAmbiguous |= Match && Match != Ability;
					Match = Ability;
				}
			}
		};
		if (Source)
		{
			FindMatch(Source->GetStartupAbilityClasses());
		}
		const float Duration = Match && !bAmbiguous && FMath::IsFinite(Match->CooldownDuration) && Match->CooldownDuration >= 0.f
			? Match->CooldownDuration : -1.f;
		bChanged |= Option.CooldownDuration != Duration;
		Option.CooldownDuration = Duration;
	}
	return bChanged;
}

void UBTTask_BossSelectAttack::PostEditChangeChainProperty(FPropertyChangedChainEvent& Event)
{
	int32 ChangedAbilityIndex = INDEX_NONE;
	for (auto* Node = Event.PropertyChain.GetHead(); Node; Node = Node->GetNextNode())
	{
		if (Node->GetValue()->GetFName() == GET_MEMBER_NAME_CHECKED(FBossAttackOption, AbilityTag))
		{
			ChangedAbilityIndex = Event.GetArrayIndex(TEXT("AttackOptions"));
			break;
		}
	}
	InitializeCooldownDefaults(ChangedAbilityIndex);
	Super::PostEditChangeChainProperty(Event);
}
#endif

FString UBTTask_BossSelectAttack::GetStaticDescription() const
{
	FString Description = Super::GetStaticDescription();
	for (const FBossAttackOption& Option : AttackOptions)
	{
		if (Option.AbilityTag.IsValid() && (!FMath::IsFinite(Option.CooldownDuration) || Option.CooldownDuration < 0.f))
		{
			Description += FString::Printf(TEXT("\nUnresolved cooldown: %s. Check Default Cooldown Source or enter seconds."),
				*Option.AbilityTag.ToString());
		}
	}
	return Description;
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
	SyncAttackCooldowns(*ASC, AttackOptions);

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
