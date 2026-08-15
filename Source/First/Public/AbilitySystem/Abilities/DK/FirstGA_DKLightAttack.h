#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKLightAttack.generated.h"

class UAbilityTask_WaitInputPress;

UCLASS()
class FIRST_API UFirstGA_DKLightAttack : public UFirstDKGameplayAbility
{
	GENERATED_BODY()

public:
	// 作用：设置攻击身份、Attacking OwnedTag 和闪避/死亡 BlockedTag。
	UFirstGA_DKLightAttack();

protected:
	// 作用：验证当前武器，初始化第一段并启动事件监听与 Montage 状态机。
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// 作用：无论正常、取消或被打断，都关闭碰撞、结束输入任务并重置连击临时状态。
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	// 作用：在 Montage 前创建持续监听的命中、连击窗口开、连击窗口关三个 GameplayEvent 任务。
	void StartEventListeners();

	// 作用：按 CurrentComboStep 选择当前武器数组中的 Montage 并开始播放。
	void StartCurrentComboStep();
	
	// 向 ASC 添加窗口授予的取消权限标签。
	void AddActionCancelTags(const FGameplayTagContainer& TagsToAdd);
	
	// 从 ASC 移除一个窗口结束时归还的取消权限标签。
	void RemoveActionCancelTags(const FGameplayTagContainer& TagsToRemove);
	
	// 攻击切段、正常结束或被打断时的最终安全清理。
	void ClearActionCancelTags();
	
	// 判断当前段后面是否真的还有下一段攻击。
    // CurrentComboStep 使用 1 起始编号；下一个蒙太奇的数组索引正好等于 CurrentComboStep。
	bool CanTransitionToNextComboStep();

	// 在 ComboWindow 结束时，请求平滑停止当前攻击蒙太奇。
	// 真正进入下一段的时机由 HandleMontageCancelled 处理。
	void RequestNextComboStepTransition();

	// 作用：收到武器命中目标事件后，创建原生 Damage GE Spec 并应用给 Payload.Target。
	UFUNCTION()
	void HandleMeleeHit(FGameplayEventData Payload);

	// 作用：连击窗口打开时开始等待下一次攻击输入。
	UFUNCTION()
	void HandleComboWindowOpened(FGameplayEventData Payload);
	
	UFUNCTION()
	void HandleActionCancelWindowOpened(FGameplayEventData Payload);
	
	UFUNCTION()
	void HandleActionCancelWindowClosed(FGameplayEventData Payload);

	// 作用：连击窗口关闭时停止等待，窗口外输入不会进入本次缓存。
	UFUNCTION()
	void HandleComboWindowClosed(FGameplayEventData Payload);

	// 作用：窗口内收到再次按键时，把“想接下一段”记录为 true。
	UFUNCTION()
	void HandleComboInputPressed(float TimeWaited);

	// 作用：当前段 Montage 正常结束后决定播放下一段还是结束整次 Ability。
	UFUNCTION()
	void HandleMontageCompleted();

	// 作用：Montage 被打断/取消时终止整次连击。
	UFUNCTION()
	void HandleMontageCancelled();

	// 作用：集中结束攻击 Ability，避免多个异步回调重复调用 EndAbility。
	void FinishAttack(bool bWasCancelled);

	// 当前段使用 1-based 编号，便于直接写入 ComboCount SetByCaller。
	int32 CurrentComboStep = 0;
	bool bComboWindowOpen = false;
	bool bWantsNextCombo = false;
	
	// 当前正在播放的攻击蒙太奇。
	UPROPERTY()
	TObjectPtr<UAnimMontage> CurrentAttackMontage;
	
	// true 表示这一次“中断蒙太奇”是连招流程主动触发的，
	// 不是闪避、死亡、受击等异常打断。
	bool bTransitionToNextComboStep = false;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitInputPress> ComboInputTask;
	
	// 每种权限标签当前被多少个取消窗口持有。
	// 使用计数而不是 bool，能避免未来两个窗口短暂重叠时提前移除标签。
	TMap<FGameplayTag, int32> ActionCancelTagCounts;
};