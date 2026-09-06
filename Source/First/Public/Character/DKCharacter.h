// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Character/BaseCharacter.h"
#include "TimerManager.h"
#include "DKCharacter.generated.h"

class ADKWeapon;
class UAnimMontage;
class UDKCombatComponent;
class USpringArmComponent;
class UCameraComponent;
class UDataAsset_InputConfig;
struct FInputActionValue;
class UDKUIComponent;
class UUserWidget;
class UDKTargetLockComponent;
class UDKDefenseComponent;
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
	
	// 创建并显示玩家 HUD 与 BOSS HUD。
	virtual void BeginPlay() override;
private:
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_JumpStarted(const FInputActionValue& Value);
	void Input_JumpCompleted(const FInputActionValue& Value);
	void Input_TargetLock(const FInputActionValue& Value);
	void Input_SwitchTarget(const FInputActionValue& Value);

	// 作用：按下字母区上方的 1 键时，根据当前装备状态选择装备或卸下 Ability。
	void Input_ToggleSword(const FInputActionValue& Value);
	
	// 作用：接收仍由 AbilityInputActions 直接转发的输入，例如鼠标左键轻攻击。
	void Input_AbilityInputPressed(FGameplayTag InputTag);
	// 作用：把直接 Ability 输入的松开边沿转发给 ASC。
	void Input_AbilityInputReleased(FGameplayTag InputTag);
	// 作用：模拟一次完整的“按下并松开”语义输入，供短按闪避和武器切换复用。
	void TriggerAbilityInputTap(const FGameplayTag& InputTag);

	// 原生输入的统一门控；Look 保留，不调用本函数。
	bool IsHitReactOrDead() const;
	
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
	
	// 玩家：方向来源 = 按键方向。
	virtual FVector GetDodgeInputDirection() const override;
	
	// 锁定时严格使用“主角到目标”的 Yaw；
	// 不依赖镜头插值是否已经跟到位。
	virtual float GetDodgeReferenceYaw() const override;
	
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

	// DK防御组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Defense",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UDKDefenseComponent> DKDefenseComponent;

	// 格挡招架动画
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Anim",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> GuardParryMontage;

	// 格挡破防（受击）动画
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Anim",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> GuardBreakMontage;

	// 处决动画
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Execution|Anim",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ExecutionMontage;

	// 四个方向表示“攻击者来自主角哪一侧”，不是身体倒向哪一侧。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="Combat|Hit React|Anim",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> HitReactFrontMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="Combat|Hit React|Anim",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> HitReactBackMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="Combat|Hit React|Anim",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> HitReactLeftMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="Combat|Hit React|Anim",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> HitReactRightMontage;

	// 普通受击的玩法硬直时间。动画会按这个时长计算播放速度。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="Combat|Hit React",
		meta=(AllowPrivateAccess="true", ClampMin="0.05", UIMin="0.05"))
	float HitReactDuration = 0.55f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="Combat|Death|Anim",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> DeathMontage;

	// 处决伤害 = 命中帧时玩家当前 AttackPower × 此倍率。
	// 放在玩家角色上，避免由受击的 BOSS 决定玩家处决伤害。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Execution",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float ExecutionDamageMultiplier = 10.f;
	
	// 这里只选择 BP_DKWeapon_Sword 类；不要在角色蓝图 Event Graph 中生成武器。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Weapon", meta=(AllowPrivateAccess="true"))
	TSubclassOf<ADKWeapon> DefaultWeaponClass;
	
	// 闪避结束前多少秒提前进入奔跑判定，用于消除“闪避完停顿一下才跑”的间隙。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge",meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0", UIMax="0.3"))
	float DodgeRunOverlap = 0.18f;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="UI", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UDKUIComponent> DKUIComponent;

	// 在 BP_DKCharacter 类默认值中配置：两个 HUD 控件类（用引擎基类，避免依赖尚未创建的控件基类）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI|HUD", meta=(AllowPrivateAccess="true"))
	TSubclassOf<UUserWidget> PlayerHUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI|HUD", meta=(AllowPrivateAccess="true"))
	TSubclassOf<UUserWidget> BossHUDWidgetClass;
	
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Target Lock",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UDKTargetLockComponent>TargetLockComponent;
	
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
	
	// 玩家 UI 数据组件（血量/精力广播）。
	FORCEINLINE UDKUIComponent* GetDKUIComponent() const { return DKUIComponent; }

	// IPawnUIInterface：返回玩家 UI 组件。
	virtual UPawnUIComponent* GetPawnUIComponent() const override;
	
	FORCEINLINE UDKTargetLockComponent*GetTargetLockComponent() const
	{
		return TargetLockComponent;
	}
	
	FORCEINLINE UDKDefenseComponent* GetDKDefenseComponent() const
	{
		return DKDefenseComponent;
	}

	FORCEINLINE UAnimMontage* GetGuardParryMontage() const
	{
		return GuardParryMontage;
	}

	FORCEINLINE UAnimMontage* GetGuardBreakMontage() const
	{
		return GuardBreakMontage;
	}

	FORCEINLINE UAnimMontage* GetExecutionMontage() const
	{
		return ExecutionMontage;
	}

	FORCEINLINE float GetExecutionDamageMultiplier() const
	{
		return ExecutionDamageMultiplier;
	}

	FORCEINLINE UAnimMontage* GetHitReactFrontMontage() const
	{
		return HitReactFrontMontage;
	}

	FORCEINLINE UAnimMontage* GetHitReactBackMontage() const
	{
		return HitReactBackMontage;
	}

	FORCEINLINE UAnimMontage* GetHitReactLeftMontage() const
	{
		return HitReactLeftMontage;
	}

	FORCEINLINE UAnimMontage* GetHitReactRightMontage() const
	{
		return HitReactRightMontage;
	}

	FORCEINLINE float GetHitReactDuration() const
	{
		return HitReactDuration;
	}

	FORCEINLINE UAnimMontage* GetDeathMontage() const
	{
		return DeathMontage;
	}

	// 由死亡 Ability 调用：清理角色层的跳跃、长按奔跑与目标锁定。
	void PrepareForDeath();

	// 只根据当前 Shift 长按状态返回正常移动速度；Guard/Break/Execute 结束时使用。
	FORCEINLINE float GetDesiredLocomotionSpeed() const
	{
		return bIsRunning ? RunSpeed : WalkSpeed;
	}
};
