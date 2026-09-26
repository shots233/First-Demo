#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "FirstBossRetaliationComponent.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
struct FGameplayEventData;

// 只记录受压状态；攻击选择与发动仍由行为树负责。
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class FIRST_API UFirstBossRetaliationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UFirstBossRetaliationComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retaliation")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retaliation", meta=(ClampMin="1"))
	int32 HitsRequired = 5;
	// 连续两次有效命中的最大间隔；已就绪的请求也随此时间到期。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retaliation", meta=(ClampMin="0.1", Units="s"))
	float HitResetDelay = 3.f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Retaliation")
	int32 HitCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Retaliation")
	bool bRetaliationPending = false;

	// AI 服务提供战斗上下文；本组件不读取黑板，也不直接发动攻击。
	void UpdateCombatContext(AActor* Target, bool bInCombat);
	bool CanRetaliateAgainst(const AActor* Target) const;
	// 仅由 BT 准备任务调用，精确取消普通受击能力，不移除通用 Staggered 标签。
	bool PrepareRetaliation(AActor* Target);
	void ResetPressure();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	bool IsContextUsable() const;
	bool HasProtectedState() const;
	bool HasActiveAbility(FGameplayTag AbilityTag) const;
	void HandleDamageReceived(const FGameplayEventData* Payload);
	void HandleResetTag(FGameplayTag Tag, int32 Count);
	void HandleAbilityActivated(UGameplayAbility* Ability);
	void PublishRequest();
	TWeakObjectPtr<AActor> CombatTarget;
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	FDelegateHandle DamageDelegate;
	FDelegateHandle ActivatedDelegate;
	TMap<FGameplayTag, FDelegateHandle> ResetTagDelegates;
	FTimerHandle ResetTimer;
	FTimerHandle PublishTimer;
	double LastHitTime = -1.0;
};
