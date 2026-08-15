// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Character/BaseCharacter.h"
#include "TimerManager.h"
#include "DKCharacter.generated.h"

class ADKWeapon;
class UDKCombatComponent;
class USpringArmComponent;
class UCameraComponent;
class UDataAsset_InputConfig;
struct FInputActionValue;
/**
 * 
 */
UCLASS()
class FIRST_API ADKCharacter : public ABaseCharacter
{
	GENERATED_BODY()
public:
	ADKCharacter();
	
protected:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_JumpStarted(const FInputActionValue& Value);
	void Input_JumpCompleted(const FInputActionValue& Value);

	// 作用：按下字母区上方的 1 键时，根据当前装备状态选择装备或卸下 Ability。
	void Input_ToggleSword(const FInputActionValue& Value);
	
	// 作用：接收仍由 AbilityInputActions 直接转发的输入，例如鼠标左键轻攻击。
	void Input_AbilityInputPressed(FGameplayTag InputTag);
	// 作用：把直接 Ability 输入的松开边沿转发给 ASC。
	void Input_AbilityInputReleased(FGameplayTag InputTag);
	// 作用：模拟一次完整的“按下并松开”语义输入，供短按闪避和武器切换复用。
	void TriggerAbilityInputTap(const FGameplayTag& InputTag);
	
	// 统一修改跑步状态与移动速度，避免多个输入函数直接写 MaxWalkSpeed。
	void SetRunning(bool bNewRunning);
	
	// 计时器到点：如果闪避键仍未松开，进入奔跑。
	void HandleDodgeHoldElapsed();
	
	// 闪避键是否仍被按住。
	// true 表示玩家在闪避播放期间没有松开，闪避结束后应进入奔跑。
	bool bDodgeInputHeld = false;
	
	// “闪避结束后检查是否仍按住”的计时器。
	// 在按下时启动，在松开时清除。
	FTimerHandle DodgeHoldRunTimerHandle;
	
	
	
	// 相机摇臂负责保存镜头距离、偏移，并处理墙体碰撞。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera",meta=(AllowPrivateAccess="true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	// 实际用于渲染玩家视角的跟随相机。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Character Data", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UDataAsset_InputConfig> InputConfigDataAsset;
	
	// 默认步行速度。第一阶段让角色出生后先处于 Walk 状态。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Character Data|Movement", meta=(AllowPrivateAccess="true"))
	float WalkSpeed = 250.f;
	
	// Shift 长按成立后使用的速度。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Character Data|Movement", meta=(AllowPrivateAccess="true"))
	float RunSpeed = 500.f;
	
	// 当前是否已经通过长按进入跑步状态。只作为角色本地移动状态，不需要进入 GAS。
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly,Category="Character Data|Movement", meta=(AllowPrivateAccess="true"))
	bool bIsRunning = false;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UDKCombatComponent> DKCombatComponent;

	// 这里只选择 BP_DKWeapon_Sword 类；不要在角色蓝图 Event Graph 中生成武器。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Weapon", meta=(AllowPrivateAccess="true"))
	TSubclassOf<ADKWeapon> DefaultWeaponClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> DodgeMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(AllowPrivateAccess="true"))
	float DodgeImpulse = 650.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(AllowPrivateAccess="true"))
	float DodgeDuration = 0.35f;
	
	// 闪避结束前多少秒提前进入奔跑判定，用于消除“闪避完停顿一下才跑”的间隙。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge",meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", UIMax="0.3"))
	float DodgeRunOverlap = 0.18f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(AllowPrivateAccess="true"))
	bool bDodgeUsesRootMotion = false;
	
public:
	// 作用：提供 DK 专用战斗组件的快捷访问，使 C++ Ability 不需要反复 FindComponent。
	FORCEINLINE UDKCombatComponent* GetDKCombatComponent() const
	{
		return DKCombatComponent;
	}

	// 作用：给出生时自动执行的 C++ Ability 提供默认武器类；行为仍由 Ability 实现。
	FORCEINLINE TSubclassOf<ADKWeapon> GetDefaultWeaponClass() const
	{
		return DefaultWeaponClass;
	}

	// 作用：让闪避 Ability 读取表现资源，不需要为设置 Montage 再创建一个 Ability 蓝图。
	FORCEINLINE UAnimMontage* GetDodgeMontage() const
	{
		return DodgeMontage;
	}

	// 作用：提供无 Root Motion 时的 C++ 位移强度。
	FORCEINLINE float GetDodgeImpulse() const { return DodgeImpulse; }

	// 作用：提供没有 Montage 时的占位持续时间，也决定闪避状态最短保持多久。
	FORCEINLINE float GetDodgeDuration() const { return DodgeDuration; }

	// 作用：区分动画根运动和 LaunchCharacter 位移，避免两套位移叠加。
	FORCEINLINE bool DoesDodgeUseRootMotion() const { return bDodgeUsesRootMotion; }
};
