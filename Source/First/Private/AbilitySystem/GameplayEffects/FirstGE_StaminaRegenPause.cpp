#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegenPause.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "MyGameplayTags.h"

UFirstGE_StaminaRegenPause::UFirstGE_StaminaRegenPause()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude =
		FGameplayEffectModifierMagnitude(FScalableFloat(0.75f));

	// 同一目标只保留一层；新一次格挡会刷新剩余 0.75 秒。
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
	StackLimitCount = 1;
	StackDurationRefreshPolicy =EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
	StackExpirationPolicy =EGameplayEffectStackingExpirationPolicy::ClearEntireStack;

	// UE 5.6 使用 GameplayEffectComponent 授予标签，
	// 不再写已弃用的 InheritableOwnedTagsContainer。
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(MyGameplayTags::DK_Status_StaminaRegenPaused);

	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
	CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));

	GEComponents.Add(TargetTagsComponent);
	TargetTagsComponent->SetAndApplyTargetTagChanges(GrantedTags);
}