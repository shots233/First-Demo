// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKEquipSword.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UFirstGA_DKEquipSword : public UFirstDKGameplayAbility
{
	GENERATED_BODY()
public:
	// 作用：设置装备技能的原生 Tag、实例策略和互斥状态。
	UFirstGA_DKEquipSword();
	
protected:
	// 作用：把剑挂到手上、登记当前装备 Tag，并授予武器攻击 Ability。
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
	
private:
	UFUNCTION()
	void HandleAttachToHand(FGameplayEventData Payload);
	
	// 真正执行“挂到手部 + 授予攻击技能”的函数。
	bool AttachSwordToHand();
	
	// 每次装备 Ability 激活时会重置，用来确认 Montage 是否正确触发了 Notify。
	bool bSwordAttachedToHand = false;
	
	// 作用：装备 Montage 正常结束后结束 Ability；装备状态已在播放前原子完成。
	UFUNCTION()
	void HandleMontageCompleted();

	// 作用：Montage 被取消或打断时安全结束 Ability，避免技能一直保持 Active。
	UFUNCTION()
	void HandleMontageCancelled();

	// 作用：集中调用 EndAbility，避免多个 Montage 回调复制同一组运行时参数。
	void FinishEquip(bool bWasCancelled);
};
