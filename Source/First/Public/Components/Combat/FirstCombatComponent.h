// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "FirstCombatComponent.generated.h"


class AFirstWeaponBase;
class APawn;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class FIRST_API UFirstCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UFirstCombatComponent();
	
	// 作用：把生成好的武器按 Tag 存入角色库存，并绑定它的命中/离开事件回调。
	UFUNCTION(BlueprintCallable, Category="First | Combat")
	void RegisterSpawnedWeapon(FGameplayTag InWeaponTagToRegister, AFirstWeaponBase* InWeaponToRegister,
		bool bRegisterAsEquippedWeapon = false);
	
	// 作用：根据武器 Tag 从角色携带武器表中查找对应 Actor；找不到时返回 nullptr。
	UFUNCTION(BlueprintCallable, Category="First | Combat")
	AFirstWeaponBase* GetCharacterCarriedWeaponByTag(FGameplayTag InWeaponTagToGet) const;
	
	// 作用：在动画命中窗口开始/结束时开启或关闭当前武器的 Overlap 碰撞。
	UFUNCTION(BlueprintCallable, Category="First|Combat")
	void ToggleWeaponCollision(bool bShouldEnable);
	
	// 作用：读取 CurrentEquippedWeaponTag 并返回当前实际装备的武器 Actor。
	UFUNCTION(BlueprintCallable, Category="First | Combat")
	AFirstWeaponBase* GetCharacterCurrentEquippedWeapon()const;
	
	//当前装备武器标签
	UPROPERTY(BlueprintReadWrite, Category="First | Combat")
	FGameplayTag CurrentEquippedWeaponTag;
	
	// 作用：接收武器碰到目标的通知；基类留空，由 DK 子类决定如何产生战斗事件。
	virtual void OnHitTargetActor(AActor* HitActor);
	// 作用：接收武器离开目标的通知，为以后抽剑特效或音效预留入口。
	virtual void OnWeaponPulledFromTargetActor(AActor* InteractedActor);
	
protected:
	// 作用：把组件 Owner 转为 Pawn，作为命中事件中的攻击者。
	APawn* GetOwningPawn() const;
	
	// 记录本次“碰撞开启窗口”已命中的对象，避免一剑停在敌人身上时重复结算。
	// 碰撞关闭时必须清空，为下一段攻击重新开始统计。
	TArray<TObjectPtr<AActor>> OverlappedActors;

public:	
	UPROPERTY()
	TMap<FGameplayTag, TObjectPtr<AFirstWeaponBase>> CharacterCarriedWeaponMap;

		
};
