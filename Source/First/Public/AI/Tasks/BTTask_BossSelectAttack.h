// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GameplayTagContainer.h"
#include "BTTask_BossSelectAttack.generated.h"

class UDataAsset_StartUpDataBase;

/**
 * 
 */

USTRUCT(BlueprintType)
struct FBossAttackOption
{
	GENERATED_BODY()

	// 招式的身份标签（AbilityTags 里的那个）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FGameplayTag AbilityTag;

	// 该招式的冷却标签（冷却中不会选中）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FGameplayTag CooldownTag;

	// 编辑器从技能默认值初始化后直接显示秒数。0 不添加招式冷却。
	// 最近执行的选招节点整表生效，也适用于表内招式的派生接招；前闪追击仍使用自己的配置。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", meta=(ClampMin="0.0", Units="s",
		ToolTip="该招式实际使用的冷却秒数，0 表示关闭。选择招式时自动填入技能原值，之后可直接修改。出招提交成功时开始计时，与收招后的休息间隔分别计算。"))
	// 负数只用于识别旧资源和待初始化的新条目；成功解析技能后不在编辑器中保留。
	float CooldownDuration = -1.f;

	// 最大使用距离（超过该距离不选）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", meta=(ClampMin="0.0"))
	float MaxRange = 300.f;

	// 最小使用距离（低于该距离不选）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", meta=(ClampMin="0.0"))
	float MinRange = 0.f;

	// 候选招式间的相对权重；0 表示禁用该选项。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", meta=(ClampMin="0.0"))
	float SelectionWeight = 1.f;

	// 0 表示无需检查；正数表示选择前必须保证身后至少有该距离的安全空间。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", meta=(ClampMin="0.0", Units="cm"))
	float RequiredRetreatSpace = 0.f;
};
UCLASS()
class FIRST_API UBTTask_BossSelectAttack : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_BossSelectAttack();

	// 在行为树节点详情里配置招式表。
	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	TArray<FBossAttackOption> AttackOptions;

	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FName AttackTagKey = TEXT("AttackTag");

#if WITH_EDITORONLY_DATA
	// 仅用来初始化编辑器中的数值。运行时使用招式表里保存的秒数。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", AdvancedDisplay,
		meta=(ToolTip="初始化冷却秒数时读取的技能列表。使用与该 BOSS 一致的出生数据；已填写的冷却不会因来源变化而被覆盖。"))
	TSoftObjectPtr<UDataAsset_StartUpDataBase> DefaultCooldownSource;
#endif

#if WITH_EDITOR
	// 保留已填写的秒数；更换招式身份时仅重填该条目。
	bool InitializeCooldownDefaults(int32 ChangedAbilityIndex = INDEX_NONE);
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;
	virtual FString GetStaticDescription() const override;

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};
