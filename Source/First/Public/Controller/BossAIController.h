// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BossAIController.generated.h"

class ABossCharacter;
class ADKCharacter;
class UAbilitySystemComponent;
class UAISenseConfig_Sight;
class UBlackboardComponent;
struct FGameplayEventData;
/**
 * 
 */
UCLASS()
class FIRST_API ABossAIController : public AAIController
{
	GENERATED_BODY()
public:
	ABossAIController();

	// 在编辑器（BP_Boss 的 AIController 默认值）中配置：巡逻行为树资产。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI")
	TObjectPtr<UBehaviorTree> BossBehaviorTree;

	// 视觉感知配置对象，可在编辑器中直接调数值。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	// 索敌半径：玩家进入这个距离且在视野内会被发现（决策书 D3：800～1200）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Perception", meta=(ClampMin="100.0"))
	float SightRadius = 1000.f;

	// 走出多远后判定“看不到”（一般比 SightRadius 大一些，防止在边界抖动）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Perception", meta=(ClampMin="100.0"))
	float LoseSightRadius = 1400.f;

	// 视野半角：左右各 60°，合计 120°（决策书 D4）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Perception", meta=(ClampMin="0.0", ClampMax="180.0"))
	float PeripheralVisionAngleDegrees = 60.f;

	// 视觉刺激的最大记忆年龄（旧属性名沿用 LoseSightTime）。
	// 注意：它不是“看不见后延迟多久才收到感知失败回调”；
	// 正式入战后视觉只提供感知信息，不再作为脱战条件。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Perception", meta=(ClampMin="0.5"))
	float LoseSightTime = 5.f;

	// 正常战斗状态下，目标水平位置允许偏离 NavMesh 的最大距离。
	// 这里比较的是“目标位置与导航投射点的水平距离”，不是投射查询盒大小。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Navigation",
		meta=(ClampMin="0.0", Units="cm"))
	float NavMeshExitTolerance = 20.f;

	// 一旦目标被判为不可达，必须更接近 NavMesh 才解除抑制，形成迟滞。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Navigation",
		meta=(ClampMin="0.0", Units="cm"))
	float NavMeshReentryTolerance = 5.f;

	// ActorLocation 通常位于胶囊中心，垂直查询范围需要覆盖到角色脚下的导航面。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Navigation",
		meta=(ClampMin="1.0", Units="cm"))
	float NavMeshVerticalProjectionExtent = 200.f;

	// 完整路径查询比单点投射昂贵；同一目标在短时间且双方几乎未移动时复用结果。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Navigation",
		meta=(ClampMin="0.0", Units="s"))
	float PathReachabilityRefreshInterval = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Navigation",
		meta=(ClampMin="0.0", Units="cm"))
	float PathReachabilityMovementTolerance = 20.f;

	// 双方已经非常接近时无需生成至少包含两个点的路径。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Navigation",
		meta=(ClampMin="0.0", Units="cm"))
	float PathReachabilityGoalTolerance = 50.f;

	// 快捷访问当前控制的 BOSS。
	UFUNCTION(BlueprintCallable, Category="Boss|AI")
	ABossCharacter* GetBossCharacter() const;

	// 强制入战期间发生的视觉丢失会延迟到特殊任务结束时处理。
	// 返回 true 表示延迟记录仍然有效，并且当前目标状态已经被清理。
	bool ResolveDeferredPerceptionLoss();

	// 处决等事务收尾时主动重建仇恨：写入 TargetActor/bPlayerDetected/bShouldChase。
	// 目标不可达时只保留目标、不开追击（与 HandleDamageReceived 的抑制口径一致）；
	// 目标无效或 BOSS 已死亡时不做任何事。
	void ForceEngageTarget(AActor* Target);

	// 距离服务保留不可达目标用于重新检测，但其它系统不得继续注视或面向它。
	void SetTargetNavigationBlocked(AActor* Target, bool bBlocked);
	bool IsTargetNavigationBlocked(const AActor* Target) const;

	// 立即根据世界导航数据刷新目标状态，返回 true 表示目标当前应被视为不可达。
	// 伤害和感知回调也必须走这里，不能等下一个行为树服务 Tick 再判断。
	bool RefreshTargetNavigationBlocked(AActor* Target);

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	// 感知事件：看到/丢失玩家时更新黑键。
	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
	
	// 初始化黑板键初值，并启动行为树。
	void InitializeBossBlackboard();

private:
	// 一旦追击已经提交、正在拔剑或已经拔剑，视觉丢失不能结束 BOSS 战。
	// 处决事务期间（破韧/被处决/起立）同样保留：处决本身证明目标已锁定，
	// 战斗状态由收尾的 ForceEngageTarget 重建。
	bool ShouldRetainCombatTargetAfterSightLoss(
		const UBlackboardComponent& BlackboardComponent) const;

	void BindDamageReceivedEvent();
	void UnbindDamageReceivedEvent();
	void HandleDamageReceived(const FGameplayEventData* Payload);

	ADKCharacter* ResolveDamageInstigator(
		const FGameplayEventData& Payload) const;

	TWeakObjectPtr<UAbilitySystemComponent> DamageReceivedEventASC;
	FDelegateHandle DamageReceivedEventHandle;

	// 不直接吞掉强制入战期间的视觉丢失通知；任务收尾时再消费。
	bool bHasDeferredPerceptionLoss = false;
	TWeakObjectPtr<AActor> DeferredLostTarget;

	// 目标暂时离开 NavMesh 时仍保留弱引用，以便其返回后无需等待视觉状态切换即可重新警戒。
	TWeakObjectPtr<AActor> NavigationBlockedTarget;

	// 完整路径查询缓存：BT Service、感知回调和攻击 Ability 可能在同一帧复查同一目标。
	TWeakObjectPtr<AActor> CachedReachabilityTarget;
	FVector CachedReachabilityBossLocation = FVector::ZeroVector;
	FVector CachedReachabilityTargetLocation = FVector::ZeroVector;
	double CachedReachabilityQueryTime = -1.0;
	bool bCachedTargetNavigationBlocked = true;
};
