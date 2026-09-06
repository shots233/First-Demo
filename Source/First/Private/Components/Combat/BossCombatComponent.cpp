// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/Combat/BossCombatComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Items/Weapons/FirstWeaponBase.h"
#include "MyGameplayTags.h"

void UBossCombatComponent::OnHitTargetActor(AActor* HitActor)
{
	Super::OnHitTargetActor(HitActor);
	// 同一碰撞窗口内同一目标只发一次事件，避免重复结算。
	// 第二道门：死亡目标（如玩家尸体）不发命中事件（武器入口已过滤，这里兜住其它调用路径）。
	if (!HitActor || OverlappedActors.Contains(HitActor) ||
		AFirstWeaponBase::IsTargetDead(HitActor))
	{
		return;
	}

	OverlappedActors.AddUnique(HitActor);

	FGameplayEventData EventData;
	EventData.Instigator = GetOwningPawn();
	EventData.Target = HitActor;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		GetOwningPawn(),
		MyGameplayTags::DK_Event_MeleeHit,
		EventData);
}
