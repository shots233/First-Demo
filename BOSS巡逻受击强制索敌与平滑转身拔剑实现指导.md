# BOSS 巡逻受击强制索敌、平滑转身与拔剑实现指导

> 适用项目：First（UE 5.6 中文版 / C++ / GAS / Behavior Tree）  
> 编写依据：当前工程中的 BossAIController、BTTask_BossAlert、BTService_UpdateBossState、FirstAttributeSet 与现有拔剑能力  
> 本册目标：BOSS 在巡逻或普通警戒阶段被主角从背后命中后，立即停止原行为，约 0.25 秒平滑转向主角，然后拔剑并进入追击  
> 重要说明：这是一份实施指导。创建本文件不会修改任何 C++、蓝图、行为树或 UE 资产

---

## 0. 完成后的最终效果

~~~text
BOSS 正在巡逻，主角从背后造成一次非致命伤害
  ↓
BossAIController 收到现有 Shared.Event.DamageReceived
  ↓
确认真正的攻击者是 ADKCharacter
  ↓
停止 Move To 和残余移动速度
  ↓
黑板 bProvokedByDamage = true
  ↓
行为树立即中止巡逻/普通警戒，进入“受袭强制入战”分支
  ↓
如果 BOSS 正在受击硬直，则先等硬直结束
  ↓
0.25 秒内沿最短 Yaw 方向平滑转向主角
  ↓
bShouldChase = true
  ↓
尚未拔剑：发送 Boss.Event.CombatStart，播放现有拔剑能力
已经拔剑：跳过拔剑
  ↓
受袭分支继续保持最高优先级，等待 WeaponDrawn
  ↓
清除 bProvokedByDamage
  ↓
进入现有攻击 / 后退 / 周旋 / 靠近逻辑
~~~

这个流程有四个明确边界：

1. 正常“看见主角”的视觉警戒仍保持原来的 3 秒，不修改 BTTask_BossAlert；
2. 只有受到主角造成的真实、非致命生命伤害时才强制入战；
3. 本册不修改目前暂时搁置的“非攻击状态不播放普通受击动画”问题；
4. 不新建 Gameplay Tag、Gameplay Ability 或动画资产。

---

## 1. 为什么采用“独立黑板状态 + 独立行为树任务”

本册不把特殊转身直接塞进现有 BTTask_BossAlert，也不在 Controller 里暂停整棵行为树。

原因是两种警戒的规则不同：

| 情况 | 应有行为 |
|---|---|
| BOSS 用视野发现主角 | 保留现有 3 秒观察，再拔剑追击 |
| BOSS 在巡逻时被主角打中 | 不再观察，立即平滑转身，再拔剑追击 |

如果把两种逻辑都写进 BTTask_BossAlert，它会同时依赖“视野警戒、伤害来源、转身时长、
是否已拔剑、是否正在硬直”等状态，后续会越来越难维护。

本册把职责拆成三层：

~~~text
BossAIController
  只负责：收到真实伤害事件、认出攻击者、写入强制入战请求

BTTask_BossDamageAlert
  只负责：停止移动、0.25 秒平滑转身、触发拔剑、交给追击

BTService_UpdateBossState
  只负责：死亡/强制入战时冻结；普通 GAS 忙碌仍按现有规则面向目标
~~~

这种拆分比把所有判断堆在一个复合装饰器里更容易调试，也保留了行为树的可视化优势。

---

## 2. 当前工程已经具备的基础

### 2.1 已有真实伤害事件

当前 FirstAttributeSet.cpp 在真实生命值下降且 BOSS 未死亡时，已经发送：

~~~text
Shared.Event.DamageReceived
~~~

事件中已经保存：

- Instigator：伤害来源；
- Target：受伤角色；
- EventMagnitude：本次实际生命伤害；
- ContextHandle：GameplayEffect 的上下文。

因此本册直接监听这个事件，不再新增伤害标签。

不要使用 Boss.Event.HitReact 作为强制索敌来源。当前普通受击 Ability 受到
Boss.Status.HitReactWindow 限制，而且 BossCharacter::HandleHealthChanged() 中使用的
GetInstigator() 也不代表“本次真正的攻击者”。它不适合承担强制索敌。

### 2.2 已有拔剑链

现有 GA_Boss_DrawSword 已由下列事件触发：

~~~text
Boss.Event.CombatStart
~~~

拔剑期间拥有：

~~~text
Boss.Status.DrawSword
~~~

武器挂到手上后拥有：

~~~text
Boss.Status.WeaponDrawn
~~~

所以新任务只需要在转身完成后发送现有 CombatStart；已经拔剑时跳过事件即可。

### 2.3 已有三态旋转模式

ABossCharacter 当前已有：

~~~cpp
enum class EBossRotationMode : uint8
{
	OrientToMovement,
	FaceTarget,
	Frozen
};
~~~

本册转身期间使用 Frozen，防止 CharacterMovement、AI Focus 和行为树任务同时抢转向；
实际 Yaw 由新任务逐帧写入。

---

## 3. 改动总览

### 3.1 新建的 C++ 类

| 新类 | 父类 | 头文件 | 源文件 |
|---|---|---|---|
| UBTTask_BossDamageAlert | UBTTaskNode | Source/First/Public/AI/Tasks/BTTask_BossDamageAlert.h | Source/First/Private/AI/Tasks/BTTask_BossDamageAlert.cpp |

在 Unreal Editor 的“新建 C++ 类”向导中：

1. 点击“显示所有类”；
2. 搜索并选择 BTTaskNode 作为父类；
3. 类名填写 BTTask_BossDamageAlert；
4. 不要手动输入开头的 U；
5. 让头文件进入 Public/AI/Tasks；
6. 让源文件进入 Private/AI/Tasks。

### 3.2 修改的现有文件

| 文件 | 作用 |
|---|---|
| BossAIController.h/.cpp | 监听真实受伤事件，解析主角攻击者，写入强制入战请求 |
| BTService_UpdateBossState.cpp | 强制入战期间维持 Frozen 并停止残余移动 |
| UBTService_UpdateCombatDistance.cpp | 无目标或目标死亡时完整清理强制入战与追击状态 |
| BB_Boss | 新增 Bool：bProvokedByDamage |
| BT_Boss | 新增高优先级“受袭强制入战”分支 |

### 3.3 明确不改的内容

- 不修改 FirstAttributeSet；
- 不修改 BTTask_BossAlert；
- 不修改 GA_Boss_DrawSword；
- 不修改 GA_Boss_HitReact；
- 不新增 Gameplay Tag；
- 不修改攻击、周旋、后退、处决或普通受击动画。

First.Build.cs 当前已经包含 GameplayAbilities、GameplayTags 和 AIModule，本册无需新增模块。

---

# 第一部分：建立强制入战状态

## 4. 在 BB_Boss 新增黑板键

打开 BB_Boss，新增：

| Key Name | Key Type | 默认值 | 说明 |
|---|---|---|---|
| bProvokedByDamage | Bool | false | BOSS 已被真实伤害唤醒，必须完成特殊转身并进入战斗 |

不要复用以下任何一个旧键：

~~~text
bPlayerDetected
bInAlertRange
bShouldChase
bIsBusy
~~~

它们的含义分别是“有目标、处于警戒距离、已经进入追击、正在执行 GAS 动作”，不能准确表示
“正在处理一次受伤唤醒”。

强制入战分支也不要依赖 bInAlertRange。玩家可能从远处、背后或范围边缘造成伤害，
UpdateCombatDistance 下一次更新就可能改变距离键，导致流程被错误中止。

---

## 5. 修改 BossAIController.h

文件：

~~~text
Source/First/Public/Controller/BossAIController.h
~~~

### 5.1 增加前置声明

在现有 ABossCharacter、UAISenseConfig_Sight 前置声明附近加入：

~~~cpp
class ADKCharacter;
class UAbilitySystemComponent;
struct FGameplayEventData;
~~~

### 5.2 增加 OnUnPossess

在 protected: 中、现有 OnPossess 下方加入：

~~~cpp
	virtual void OnUnPossess() override;
~~~

### 5.3 增加监听工具与句柄

在现有 public: 区域、GetBossCharacter() 下方加入：

~~~cpp
	// 消费强制入战期间暂存的视觉丢失事件。
	// 返回 true 表示当前目标确实仍处于丢失状态，并且已经完成目标清理。
	bool ResolveDeferredPerceptionLoss();

	// 目标离开 NavMesh 时保留引用用于复查，但禁止其它系统继续注视它。
	void SetTargetNavigationBlocked(AActor* Target, bool bBlocked);
	bool IsTargetNavigationBlocked(const AActor* Target) const;
~~~

然后在类末尾增加 private: 区域：

~~~cpp
private:
	// 绑定 / 解绑 BOSS ASC 上的真实受伤事件。
	void BindDamageReceivedEvent();
	void UnbindDamageReceivedEvent();

	// Shared.Event.DamageReceived 的原生委托回调。
	// 这不是动态委托，因此不需要 UFUNCTION()。
	void HandleDamageReceived(const FGameplayEventData* Payload);

	// 从 GameplayEvent 的 Instigator / Context / Owner 链中解析真正的主角。
	ADKCharacter* ResolveDamageInstigator(
		const FGameplayEventData& Payload) const;

	// 当前绑定的 ASC 使用弱引用，避免 Controller 比 Pawn 活得更久时留下悬空对象。
	TWeakObjectPtr<UAbilitySystemComponent> DamageReceivedEventASC;
	FDelegateHandle DamageReceivedEventHandle;

	// 强制入战期间不能立即清 TargetActor，但也不能吞掉视觉丢失事件。
	// 任务结束时会消费这份状态并重新决定是否清理当前目标。
	bool bHasDeferredPerceptionLoss = false;
	TWeakObjectPtr<AActor> DeferredLostTarget;

	// 不可达目标的弱引用；返回 NavMesh 后由距离服务主动解除。
	TWeakObjectPtr<AActor> NavigationBlockedTarget;
~~~

这里必须同时保存 ASC 和 FDelegateHandle。只绑定而不解绑，会在重新占有 Pawn 或重开关卡后
产生重复回调。

---

## 6. 修改 BossAIController.cpp 的 include

文件：

~~~text
Source/First/Private/Controller/BossAIController.cpp
~~~

当前文件已经包含 AbilitySystemComponent、GameplayAbilityTypes、BossCharacter 和
MyGameplayTags。继续加入：

~~~cpp
#include "Character/DKCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
~~~

---

## 7. 替换 OnPossess，并新增 OnUnPossess

把当前 OnPossess() 整体替换为：

~~~cpp
void ABossAIController::OnPossess(APawn* InPawn)
{
	// 如果 Controller 被重新用于另一个 Pawn，先解除旧 ASC 的监听。
	UnbindDamageReceivedEvent();

	Super::OnPossess(InPawn);

	if (!GetBossCharacter())
	{
		return;
	}

	if (PerceptionComponent)
	{
		// 先移除再添加，防止重新占有时重复绑定动态委托。
		PerceptionComponent->OnTargetPerceptionUpdated.RemoveDynamic(
			this,
			&ThisClass::HandleTargetPerceptionUpdated);

		PerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(
			this,
			&ThisClass::HandleTargetPerceptionUpdated);
	}

	InitializeBossBlackboard();
	BindDamageReceivedEvent();
}
~~~

在它后面新增：

~~~cpp
void ABossAIController::OnUnPossess()
{
	UnbindDamageReceivedEvent();
	bHasDeferredPerceptionLoss = false;
	DeferredLostTarget.Reset();

	if (PerceptionComponent)
	{
		PerceptionComponent->OnTargetPerceptionUpdated.RemoveDynamic(
			this,
			&ThisClass::HandleTargetPerceptionUpdated);
	}

	Super::OnUnPossess();
}
~~~

---

## 8. 绑定与解绑 Shared.Event.DamageReceived

在 BossAIController.cpp 中新增：

~~~cpp
void ABossAIController::BindDamageReceivedEvent()
{
	UnbindDamageReceivedEvent();

	ABossCharacter* Boss = GetBossCharacter();
	UAbilitySystemComponent* BossASC =
		Boss ? Boss->GetAbilitySystemComponent() : nullptr;

	if (!BossASC)
	{
		return;
	}

	DamageReceivedEventASC = BossASC;
	DamageReceivedEventHandle =
		BossASC->GenericGameplayEventCallbacks
		.FindOrAdd(MyGameplayTags::Shared_Event_DamageReceived)
		.AddUObject(
			this,
			&ThisClass::HandleDamageReceived);
}

void ABossAIController::UnbindDamageReceivedEvent()
{
	if (DamageReceivedEventHandle.IsValid())
	{
		if (UAbilitySystemComponent* BossASC =
			DamageReceivedEventASC.Get())
		{
			// 解绑时使用 Find，不能用 FindOrAdd。
			// 否则本来没有委托时反而会新建一个空条目。
			if (FGameplayEventMulticastDelegate* EventDelegate =
				BossASC->GenericGameplayEventCallbacks.Find(
					MyGameplayTags::Shared_Event_DamageReceived))
			{
				EventDelegate->Remove(
					DamageReceivedEventHandle);
			}
		}
	}

	DamageReceivedEventHandle.Reset();
	DamageReceivedEventASC.Reset();
}
~~~

GenericGameplayEventCallbacks 是“精确标签监听”。这里只响应完整的
Shared.Event.DamageReceived，不会误收其它 Shared.Event 子标签。

---

## 9. 解析真正的攻击者

武器、投射物和 GameplayEffect 的 Instigator 表达方式可能不同，因此不要只做一次 Cast。
新增：

~~~cpp
ADKCharacter* ABossAIController::ResolveDamageInstigator(
	const FGameplayEventData& Payload) const
{
	// 一个候选 Actor 可能本身就是主角，也可能是 Controller、武器或投射物。
	const auto ResolveCandidate =
		[](AActor* SourceActor) -> ADKCharacter*
		{
			if (!IsValid(SourceActor))
			{
				return nullptr;
			}

		if (ADKCharacter* Player =
			Cast<ADKCharacter>(SourceActor))
		{
			return Player;
		}

		if (AController* SourceController =
			Cast<AController>(SourceActor))
		{
			if (ADKCharacter* Player =
				Cast<ADKCharacter>(
					SourceController->GetPawn()))
			{
				return Player;
			}
		}

		if (APawn* InstigatorPawn =
			SourceActor->GetInstigator())
		{
			if (ADKCharacter* Player =
				Cast<ADKCharacter>(InstigatorPawn))
			{
				return Player;
			}
		}

		if (AActor* OwnerActor =
			SourceActor->GetOwner())
		{
			if (ADKCharacter* Player =
				Cast<ADKCharacter>(OwnerActor))
			{
				return Player;
			}

			if (AController* OwnerController =
				Cast<AController>(OwnerActor))
			{
				return Cast<ADKCharacter>(
					OwnerController->GetPawn());
			}
		}

		return nullptr;
	};

	// 三个来源都要独立尝试。
	// 不能只在前一个 Actor 无效时回退：一个“有效的武器 Actor”也可能无法直接
	// 解析到玩家，而 Context 的 OriginalInstigator 却是正确的主角。
	if (ADKCharacter* Player = ResolveCandidate(
		const_cast<AActor*>(
			Payload.Instigator.Get())))
	{
		return Player;
	}

	if (ADKCharacter* Player = ResolveCandidate(
		Payload.ContextHandle.GetOriginalInstigator()))
	{
		return Player;
	}

	return ResolveCandidate(
		Payload.ContextHandle.GetEffectCauser());
}
~~~

首版最终只接受 ADKCharacter：

- 主角近战伤害：可以唤醒；
- 主角武器或以后投射物：只要上下文能追溯到主角，也可以唤醒；
- 地形伤害、陷阱、空来源伤害：不唤醒；
- 其它 BOSS 或普通 Actor：不唤醒。

---

## 10. 实现受伤回调

继续在 BossAIController.cpp 新增：

~~~cpp
void ABossAIController::HandleDamageReceived(
	const FGameplayEventData* Payload)
{
	// AI 决策只由服务器执行。
	if (!HasAuthority() ||
		!Payload ||
		Payload->EventMagnitude <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacter();
	UAbilitySystemComponent* BossASC =
		Boss ? Boss->GetAbilitySystemComponent() : nullptr;
	UBlackboardComponent* BB =
		GetBlackboardComponent();

	if (!Boss || !BossASC || !BB)
	{
		return;
	}

	// 防止错误路由的事件唤醒当前 BOSS。
	if (const AActor* EventTarget =
		Payload->Target.Get())
	{
		if (EventTarget != Boss)
		{
			return;
		}
	}

	// 死亡与处决生命周期属于终局/强控制状态，不能被普通伤害重新拉回战斗。
	if (BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead) ||
		BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_Executable) ||
		BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_BeingExecuted) ||
		BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_Executed))
	{
		return;
	}

	ADKCharacter* Attacker =
		ResolveDamageInstigator(*Payload);

	if (!IsValid(Attacker))
	{
		return;
	}

	if (UAbilitySystemComponent* AttackerASC =
		UAbilitySystemBlueprintLibrary::
		GetAbilitySystemComponent(Attacker))
	{
		if (AttackerASC->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead))
		{
			return;
		}
	}

	// 新一笔真实伤害重新确认了攻击者，旧的视觉丢失记录已经失效。
	bHasDeferredPerceptionLoss = false;
	DeferredLostTarget.Reset();

	// 无论是否已经入战，都允许真实伤害刷新目标。
	BB->SetValueAsObject(
		TEXT("TargetActor"),
		Attacker);
	BB->SetValueAsBool(
		TEXT("bPlayerDetected"),
		true);
	SetFocus(
		Attacker,
		EAIFocusPriority::Gameplay);

	// 已进入追击或正在处理本次受击事务时，不重复启动 0.25 秒特殊转身。
	// Attacking / DrawSword 是短暂 GAS 状态，不应吞掉一次新的强制索敌请求；
	// 新任务会在这些状态结束后从当前朝向重新开始平滑转身。
	const bool bAlreadyEngaged =
		BB->GetValueAsBool(TEXT("bShouldChase")) ||
		BB->GetValueAsBool(TEXT("bProvokedByDamage"));

	if (bAlreadyEngaged)
	{
		return;
	}

	// 只在真正开始“受伤唤醒”时停止移动。
	// 如果把这段放在 bAlreadyEngaged 判断前，战斗中每次受伤都会取消当前 Move To。
	StopMovement();

	if (UCharacterMovementComponent* Movement =
		Boss->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	Boss->SetBossRotationMode(
		EBossRotationMode::Frozen);

	// 新一轮战斗从干净的对峙状态开始。
	// 距离键继续由 UpdateCombatDistance 独占写入，这里不要伪造。
	BB->SetValueAsBool(TEXT("bHasAttacked"), false);
	BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
	BB->SetValueAsBool(TEXT("bShouldChase"), false);

	// 必须最后写：这个键会立刻触发行为树 Observer，
	// 中止巡逻/普通警戒并切到高优先级分支。
	BB->SetValueAsBool(
		TEXT("bProvokedByDamage"),
		true);
}
~~~

注意：这里没有屏蔽 Boss.Status.Staggered。

如果同一次攻击成功触发普通受击硬直，bProvokedByDamage 仍然会保留；高优先级任务自身
等待 Staggered 结束，再执行平滑转身。这样不会丢失“受击后必须入战”的请求，也不会
让旧 Boss Alert 趁硬直期间重新瞬转。

---

## 11. 初始化与感知清理

### 11.1 初始化新键

在 InitializeBossBlackboard() 的索敌初值区域加入：

~~~cpp
	BB->SetValueAsBool(
		TEXT("bProvokedByDamage"),
		false);
	bHasDeferredPerceptionLoss = false;
	DeferredLostTarget.Reset();
~~~

### 11.2 目标死亡时清理

在 HandleTargetPerceptionUpdated() 的“当前目标已死亡”清理区域加入：

~~~cpp
	BB->SetValueAsBool(
		TEXT("bProvokedByDamage"),
		false);
	bHasDeferredPerceptionLoss = false;
	DeferredLostTarget.Reset();
~~~

### 11.3 受伤唤醒期间，其它视觉候选不能覆盖伤害目标

当前首版只有一名主角，通常不会遇到多目标；但这条保护可以避免以后加入诱饵、友军或
多人时，感知回调覆盖刚刚确认的攻击者。

在 HandleTargetPerceptionUpdated() 的感知成功分支、写入 TargetActor 之前加入：

~~~cpp
		// 目标重新进入视野后，上一条延迟丢失记录已经失效。
		if (bHasDeferredPerceptionLoss &&
			DeferredLostTarget.Get() == Candidate)
		{
			bHasDeferredPerceptionLoss = false;
			DeferredLostTarget.Reset();
		}

		// 强制入战期间，以真实伤害来源为最高优先级。
		AActor* DamageTarget =
			Cast<AActor>(
				BB->GetValueAsObject(
					TEXT("TargetActor")));

		if (BB->GetValueAsBool(
				TEXT("bProvokedByDamage")) &&
			IsValid(DamageTarget) &&
			DamageTarget != Candidate)
		{
			return;
		}
~~~

### 11.4 受伤唤醒期间，视觉丢失要延迟处理，不能直接吞掉

在 HandleTargetPerceptionUpdated() 的 Stimulus.WasSuccessfullySensed() == false 分支中，
找到：

~~~cpp
if (CurrentTarget == Candidate)
{
~~~

在清空 TargetActor 之前加入：

~~~cpp
			// 真实伤害已经确认了攻击者。
			// 特殊转身结束前先保留目标，但必须记住这次丢失；
			// 否则清除 bProvokedByDamage 后感知系统不会保证再次发送丢失事件。
			if (BB->GetValueAsBool(
					TEXT("bProvokedByDamage")))
			{
				bHasDeferredPerceptionLoss = true;
				DeferredLostTarget = Candidate;
				return;
			}
~~~

不要在“看到目标”分支中把 bProvokedByDamage 改回 false。这个状态只能由特殊转身任务、
目标死亡或 BOSS 死亡清理。

这里的 return 只是在当前回调中延迟清理，并不是忽略事件。若目标随后重新被看到，11.3
会撤销延迟记录；若一直没有重新看到，任务结束时必须调用下一节的复核函数。

### 11.5 实现任务结束时的视觉丢失复核

在 BossAIController.cpp 中新增：

~~~cpp
bool ABossAIController::ResolveDeferredPerceptionLoss()
{
	if (!bHasDeferredPerceptionLoss)
	{
		return false;
	}

	UBlackboardComponent* BB = GetBlackboardComponent();
	AActor* LostTarget = DeferredLostTarget.Get();

	// 先消费记录，保证这个函数重复调用也是安全的。
	bHasDeferredPerceptionLoss = false;
	DeferredLostTarget.Reset();

	if (!BB)
	{
		return false;
	}

	AActor* CurrentTarget = Cast<AActor>(
		BB->GetValueAsObject(TEXT("TargetActor")));

	// 延迟期间如果已经换成另一个有效目标，旧丢失事件不能清掉新目标。
	if (IsValid(CurrentTarget) &&
		IsValid(LostTarget) &&
		CurrentTarget != LostTarget)
	{
		return false;
	}

	// LostTarget 已被销毁但黑板已经指向一个新的有效目标时，同样保留新目标。
	if (IsValid(CurrentTarget) && !IsValid(LostTarget))
	{
		return false;
	}

	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);

	BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
	BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
	BB->SetValueAsBool(TEXT("bInAlertRange"), false);
	BB->SetValueAsBool(TEXT("bInAttackRange"), false);
	BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
	BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
	BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
	BB->SetValueAsBool(TEXT("bShouldChase"), false);
	BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
	BB->SetValueAsBool(TEXT("bHasAttacked"), false);
	BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);

	if (ABossCharacter* Boss = GetBossCharacter())
	{
		// 与现有感知丢失分支保持一致：先冻结残余路径转向，
		// 下一次 UpdateBossState Tick 会恢复 OrientToMovement。
		Boss->SetBossRotationMode(EBossRotationMode::Frozen);
	}

	return true;
}
~~~

这个方法不凭空再次查询视觉，而是消费回调已经记录的事实。感知成功回调会撤销记录，因此
任务完成时仍存在的记录，表示该目标自从丢失后没有重新进入视野。战斗尚未提交时可以清理；
一旦拔剑并锁定战斗，则只消费这条旧记录并继续保留 TargetActor。这样既不会在 0.25 秒
转身和拔剑期间提前撤销伤害目标，也不会让过期视觉事件结束已经开始的 BOSS 战。

---

# 第二部分：创建固定 0.25 秒平滑转身任务

## 12. 新建 BTTask_BossDamageAlert.h

文件：

~~~text
Source/First/Public/AI/Tasks/BTTask_BossDamageAlert.h
~~~

整体内容：

~~~cpp
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossDamageAlert.generated.h"

/**
 * 受伤强制入战：
 * 停止移动，在固定时间内沿最短 Yaw 平滑面向攻击者，
 * 然后触发现有拔剑事件并进入追击。
 */
UCLASS()
class FIRST_API UBTTask_BossDamageAlert : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BossDamageAlert();

	// 受击后转向攻击者所用的固定时间。
	UPROPERTY(
		EditAnywhere,
		Category="Boss|Damage Alert",
		meta=(ClampMin="0.0", UIMin="0.0"))
	float TurnDuration = 0.25f;

	// 已经几乎对准时允许提前完成，避免细小角度抖动。
	UPROPERTY(
		EditAnywhere,
		Category="Boss|Damage Alert",
		meta=(ClampMin="0.0", ClampMax="45.0"))
	float TurnToleranceDegrees = 2.f;

	// 防止拔剑资源或状态标签误配时永久占住最高优先级分支；0 表示关闭保护。
	UPROPERTY(
		EditAnywhere,
		Category="Boss|Damage Alert",
		meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float DrawSwordTimeout = 5.f;

protected:
	virtual uint16 GetInstanceMemorySize() const override;

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;

	virtual EBTNodeResult::Type AbortTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;

	virtual void TickTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		float DeltaSeconds) override;
};
~~~

---

## 13. 新建 BTTask_BossDamageAlert.cpp

文件：

~~~text
Source/First/Private/AI/Tasks/BTTask_BossDamageAlert.cpp
~~~

整体内容：

~~~cpp
#include "AI/Tasks/BTTask_BossDamageAlert.h"

#include "AIController.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "Controller/BossAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

namespace
{
	struct FBossDamageAlertTaskMemory
	{
		float ElapsedTime = 0.f;
		float StartYaw = 0.f;
		float TargetYawUnwrapped = 0.f;
		float DrawSwordElapsedTime = 0.f;
		bool bTurnCompleted = false;
		bool bCombatStartSent = false;
		bool bWasTurnPaused = false;
	};

	bool IsBossDead(
		const UAbilitySystemComponent* BossASC)
	{
		return !BossASC ||
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Shared_Status_Dead);
	}

	bool IsTargetDead(AActor* Target)
	{
		UAbilitySystemComponent* TargetASC =
			IsValid(Target)
			? UAbilitySystemBlueprintLibrary::
				GetAbilitySystemComponent(Target)
			: nullptr;

		return TargetASC &&
			TargetASC->HasMatchingGameplayTag(
				MyGameplayTags::Shared_Status_Dead);
	}

	bool IsBossInTerminalControl(
		const UAbilitySystemComponent* BossASC)
	{
		return !BossASC ||
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Shared_Status_Dead) ||
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_Executable) ||
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_BeingExecuted) ||
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_Executed);
	}

	// 所有失败和 Behavior Tree Abort 都走同一条清理路径。
	// 仍有效且没有延迟丢失记录的目标可以交回普通警戒；终局、死亡、
	// 目标销毁和确认丢失则完整清空，避免残留追击、Focus 或 Frozen。
	void CleanupFailedOrAbortedDamageAlert(
		ABossAIController* AIController,
		UBlackboardComponent* BB,
		ABossCharacter* Boss)
	{
		const bool bDeferredLossResolved =
			AIController &&
			AIController->ResolveDeferredPerceptionLoss();

		UAbilitySystemComponent* BossASC =
			Boss ? Boss->GetAbilitySystemComponent() : nullptr;
		AActor* Target = BB
			? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
			: nullptr;

		const bool bCanRetainTarget =
			!bDeferredLossResolved &&
			BB &&
			IsValid(Target) &&
			!IsTargetDead(Target) &&
			!IsBossInTerminalControl(BossASC);
		const bool bCanFaceTarget =
			bCanRetainTarget &&
			AIController &&
			!AIController->IsTargetNavigationBlocked(Target);

		if (BB)
		{
			BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
			BB->SetValueAsBool(TEXT("bShouldChase"), false);

			if (!bCanRetainTarget)
			{
				BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
				BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
				BB->SetValueAsBool(TEXT("bInAlertRange"), false);
				BB->SetValueAsBool(TEXT("bInAttackRange"), false);
				BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
				BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
				BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
				BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
				BB->SetValueAsBool(TEXT("bHasAttacked"), false);
			}
		}

		if (AIController)
		{
			AIController->StopMovement();
			if (bCanFaceTarget)
			{
				AIController->SetFocus(
					Target,
					EAIFocusPriority::Gameplay);
			}
			else
			{
				AIController->ClearFocus(
					EAIFocusPriority::Gameplay);
			}
		}

		if (Boss)
		{
			Boss->SetBossRotationMode(
				IsBossInTerminalControl(BossASC)
				? EBossRotationMode::Frozen
				: (bCanFaceTarget
					? EBossRotationMode::FaceTarget
					: EBossRotationMode::OrientToMovement));
		}
	}
}

UBTTask_BossDamageAlert::UBTTask_BossDamageAlert()
{
	NodeName = TEXT("Boss Damage Alert");

	// 没有这行，TickTask 不会运行，任务会永远停在 InProgress。
	INIT_TASK_NODE_NOTIFY_FLAGS();
}

uint16 UBTTask_BossDamageAlert::GetInstanceMemorySize() const
{
	return sizeof(FBossDamageAlertTaskMemory);
}

EBTNodeResult::Type
UBTTask_BossDamageAlert::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	ABossAIController* AIController =
		Cast<ABossAIController>(OwnerComp.GetAIOwner());
	UBlackboardComponent* BB =
		OwnerComp.GetBlackboardComponent();
	ABossCharacter* Boss =
		AIController
		? Cast<ABossCharacter>(
			AIController->GetPawn())
		: nullptr;
	AActor* Target =
		BB
		? Cast<AActor>(
			BB->GetValueAsObject(
				TEXT("TargetActor")))
		: nullptr;
	UAbilitySystemComponent* BossASC =
		Boss
		? Boss->GetAbilitySystemComponent()
		: nullptr;

	if (!AIController ||
		!BB ||
		!Boss ||
		!IsValid(Target) ||
		IsBossDead(BossASC) ||
		IsTargetDead(Target) ||
		AIController->IsTargetNavigationBlocked(Target) ||
		!BB->GetValueAsBool(
			TEXT("bProvokedByDamage")))
	{
		CleanupFailedOrAbortedDamageAlert(
			AIController,
			BB,
			Boss);
		return EBTNodeResult::Failed;
	}

	FBossDamageAlertTaskMemory* Memory =
		reinterpret_cast<
			FBossDamageAlertTaskMemory*>(NodeMemory);
	Memory->ElapsedTime = 0.f;
	Memory->StartYaw =
		Boss->GetActorRotation().Yaw;
	Memory->bTurnCompleted = false;
	Memory->bCombatStartSent = false;
	Memory->bWasTurnPaused = false;
	Memory->DrawSwordElapsedTime = 0.f;

	FVector InitialToTarget =
		Target->GetActorLocation() -
		Boss->GetActorLocation();
	InitialToTarget.Z = 0.f;

	const float InitialDesiredYaw =
		InitialToTarget.IsNearlyZero()
		? Memory->StartYaw
		: InitialToTarget.Rotation().Yaw;

	// 保存连续、不跨 ±180° 跳变的目标角度。
	Memory->TargetYawUnwrapped =
		Memory->StartYaw +
		FMath::FindDeltaAngleDegrees(
			Memory->StartYaw,
			InitialDesiredYaw);

	AIController->StopMovement();

	if (UCharacterMovementComponent* Movement =
		Boss->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	AIController->SetFocus(
		Target,
		EAIFocusPriority::Gameplay);
	Boss->SetBossRotationMode(
		EBossRotationMode::Frozen);

	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type
UBTTask_BossDamageAlert::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	ABossAIController* AIController =
		Cast<ABossAIController>(OwnerComp.GetAIOwner());
	UBlackboardComponent* BB =
		OwnerComp.GetBlackboardComponent();
	ABossCharacter* Boss = AIController
		? AIController->GetBossCharacter()
		: nullptr;

	CleanupFailedOrAbortedDamageAlert(
		AIController,
		BB,
		Boss);

	return EBTNodeResult::Aborted;
}

void UBTTask_BossDamageAlert::TickTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	ABossAIController* AIController =
		Cast<ABossAIController>(OwnerComp.GetAIOwner());
	UBlackboardComponent* BB =
		OwnerComp.GetBlackboardComponent();
	ABossCharacter* Boss =
		AIController
		? Cast<ABossCharacter>(
			AIController->GetPawn())
		: nullptr;
	AActor* Target =
		BB
		? Cast<AActor>(
			BB->GetValueAsObject(
				TEXT("TargetActor")))
		: nullptr;
	UAbilitySystemComponent* BossASC =
		Boss
		? Boss->GetAbilitySystemComponent()
		: nullptr;

	if (!AIController ||
		!BB ||
		!Boss ||
		!IsValid(Target) ||
		IsBossDead(BossASC) ||
		IsTargetDead(Target) ||
		AIController->IsTargetNavigationBlocked(Target) ||
		!BB->GetValueAsBool(
			TEXT("bProvokedByDamage")))
	{
		CleanupFailedOrAbortedDamageAlert(
			AIController,
			BB,
			Boss);
		FinishLatentTask(
			OwnerComp,
			EBTNodeResult::Failed);
		return;
	}

	FBossDamageAlertTaskMemory* Memory =
		reinterpret_cast<
			FBossDamageAlertTaskMemory*>(NodeMemory);

	// 可处决及处决生命周期不是短暂受击硬直，不能在最高优先级分支里无限等待。
	const bool bExecutionControlled =
		BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_Executable) ||
		BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_BeingExecuted) ||
		BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_Executed);

	if (bExecutionControlled)
	{
		CleanupFailedOrAbortedDamageAlert(
			AIController,
			BB,
			Boss);
		FinishLatentTask(
			OwnerComp,
			EBTNodeResult::Failed);
		return;
	}

	// 拔剑标签异常残留或 Montage 误设循环时，不能永久卡住最高优先级任务。
	const bool bDrawSwordActive =
		BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_DrawSword);
	if (bDrawSwordActive)
	{
		Memory->DrawSwordElapsedTime +=
			FMath::Max(DeltaSeconds, 0.f);
		if (DrawSwordTimeout > KINDA_SMALL_NUMBER &&
			Memory->DrawSwordElapsedTime >= DrawSwordTimeout)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[Boss Damage Alert] 等待拔剑完成超过 %.2f 秒，"
					 "请检查 Montage 循环和 DrawSword 标签。"),
				DrawSwordTimeout);
			CleanupFailedOrAbortedDamageAlert(
				AIController,
				BB,
				Boss);
			FinishLatentTask(
				OwnerComp,
				EBTNodeResult::Failed);
			return;
		}
	}
	else
	{
		Memory->DrawSwordElapsedTime = 0.f;
	}

	// Staggered 是短暂状态：暂停任务，但记住转身基准已经可能被 Montage 改变。
	const bool bHardControlled =
		BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_Staggered);

	if (bHardControlled)
	{
		if (!Memory->bTurnCompleted)
		{
			Memory->bWasTurnPaused = true;
		}
		return;
	}

	// 极端竞态：如果别的系统在转身完成前启动了攻击或拔剑，
	// 等它结束，不让手动转身与 Montage 争夺身体。
	if (!Memory->bTurnCompleted &&
		(BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_Attacking) ||
		 bDrawSwordActive))
	{
		Memory->bWasTurnPaused = true;
		return;
	}

	// 硬直或其它 Montage 可能改变 Actor Yaw。恢复后必须从“当前朝向”重新开始
	// 一个完整 TurnDuration，不能继续使用进入硬控前的 StartYaw，否则会被旧插值拉回。
	if (!Memory->bTurnCompleted && Memory->bWasTurnPaused)
	{
		Memory->ElapsedTime = 0.f;
		Memory->StartYaw = Boss->GetActorRotation().Yaw;

		FVector RestartToTarget =
			Target->GetActorLocation() - Boss->GetActorLocation();
		RestartToTarget.Z = 0.f;

		const float RestartDesiredYaw =
			RestartToTarget.IsNearlyZero()
			? Memory->StartYaw
			: RestartToTarget.Rotation().Yaw;

		Memory->TargetYawUnwrapped =
			Memory->StartYaw +
			FMath::FindDeltaAngleDegrees(
				Memory->StartYaw,
				RestartDesiredYaw);
		Memory->bWasTurnPaused = false;
	}

	if (!Memory->bTurnCompleted)
	{
		Memory->ElapsedTime +=
			FMath::Max(DeltaSeconds, 0.f);

		FVector ToTarget =
			Target->GetActorLocation() -
			Boss->GetActorLocation();
		ToTarget.Z = 0.f;

		const float DesiredYaw =
			ToTarget.IsNearlyZero()
			? Boss->GetActorRotation().Yaw
			: ToTarget.Rotation().Yaw;

		// 把每一帧的新目标角连续解包。
		// 玩家跨过 +180° / -180° 时，目标会从 179° 连续变为 181°，
		// 不会突然改成 -179° 并让 BOSS 中途反向。
		Memory->TargetYawUnwrapped +=
			FMath::FindDeltaAngleDegrees(
				FRotator::NormalizeAxis(
					Memory->TargetYawUnwrapped),
				DesiredYaw);

		const float RawAlpha =
			TurnDuration <= KINDA_SMALL_NUMBER
			? 1.f
			: FMath::Clamp(
				Memory->ElapsedTime / TurnDuration,
				0.f,
				1.f);

		// SmoothStep：起步和收尾都较柔和，同时仍能保证固定时长到位。
		const float SmoothAlpha =
			RawAlpha * RawAlpha *
			(3.f - 2.f * RawAlpha);

		const float NewYaw =
			FRotator::NormalizeAxis(
				Memory->StartYaw +
				(Memory->TargetYawUnwrapped -
				 Memory->StartYaw) *
				SmoothAlpha);

		Boss->SetActorRotation(
			FRotator(0.f, NewYaw, 0.f));

		const float RemainingYaw =
			FMath::Abs(
				FMath::FindDeltaAngleDegrees(
					Boss->GetActorRotation().Yaw,
					DesiredYaw));

		if (RawAlpha < 1.f &&
			RemainingYaw > TurnToleranceDegrees)
		{
			return;
		}

		// 第一阶段结束：最后一帧精确面对目标当前位置。
		Boss->SetActorRotation(
			FRotator(0.f, DesiredYaw, 0.f));
		AIController->SetFocus(
			Target,
			EAIFocusPriority::Gameplay);

		Memory->bTurnCompleted = true;

		// 可以进入战斗，但 bProvokedByDamage 继续保持 true，
		// 因此高优先级分支仍占有行为树，拔剑期间不会启动 Move To。
		BB->SetValueAsBool(
			TEXT("bPlayerDetected"),
			true);
		BB->SetValueAsBool(
			TEXT("bShouldChase"),
			true);

		const bool bWeaponAlreadyDrawn =
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_WeaponDrawn);
		const bool bDrawSwordAlreadyRunning =
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_DrawSword);

		if (!bWeaponAlreadyDrawn &&
			!bDrawSwordAlreadyRunning)
		{
			// 先记为已发送，防止事件同步回调期间产生重入时重复发送。
			Memory->bCombatStartSent = true;

			FGameplayEventData EventData;
			EventData.EventTag =
				MyGameplayTags::Boss_Event_CombatStart;
			EventData.Instigator = Target;
			EventData.Target = Boss;

			UAbilitySystemBlueprintLibrary::
			SendGameplayEventToActor(
				Boss,
				MyGameplayTags::Boss_Event_CombatStart,
				EventData);
		}
	}

	// 第二阶段：强制分支一直等到拔剑结束。
	// 这不依赖 UpdateBossState 何时把 DrawSword 同步成 bIsBusy，
	// 因而不存在“拔剑首帧先跑一步”的空档。
	const bool bDrawingNow =
		BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_DrawSword);
	const bool bWeaponDrawnNow =
		BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_WeaponDrawn);

	if (bDrawingNow)
	{
		return;
	}

	if (bWeaponDrawnNow)
	{
		// 视觉丢失在强制事务期间只是延迟，完成前必须消费。
		// 已经拔剑且目标仍在领地内时只消费记录、保留目标；
		// 只有尚未锁定战斗或目标已经离开领地时才允许清理。
		if (AIController->ResolveDeferredPerceptionLoss())
		{
			CleanupFailedOrAbortedDamageAlert(
				AIController,
				BB,
				Boss);
			FinishLatentTask(
				OwnerComp,
				EBTNodeResult::Failed);
			return;
		}

		BB->SetValueAsBool(
			TEXT("bProvokedByDamage"),
			false);
		Boss->SetBossRotationMode(
			EBossRotationMode::FaceTarget);
		AIController->SetFocus(
			Target,
			EAIFocusPriority::Gameplay);
		FinishLatentTask(
			OwnerComp,
			EBTNodeResult::Succeeded);
		return;
	}

	// SendGameplayEventToActor 是同步分发。
	// 如果发送后既没有 DrawSword 也没有 WeaponDrawn，通常说明能力未授予或被错误阻止。
	// 这里清请求，避免行为树永久卡在特殊分支。
	if (Memory->bCombatStartSent)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[Boss Damage Alert] CombatStart 后未进入 DrawSword，"
				 "请检查 GA_Boss_DrawSword 是否已授予以及触发标签。"));
	}
	else
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[Boss Damage Alert] BOSS 既未拔剑也不在拔剑中。"));
	}

	CleanupFailedOrAbortedDamageAlert(
		AIController,
		BB,
		Boss);
	FinishLatentTask(
		OwnerComp,
		EBTNodeResult::Failed);
}
~~~

### 13.1 为什么不用 RInterpTo

RInterpTo 的 InterpSpeed 表示“接近目标的速度”，不是“多少秒一定完成”。角度越大，实际
完成时间越长，而且它是渐近接近，不能保证严格在 0.25 秒到位。

本任务使用：

~~~text
RawAlpha = ElapsedTime / 0.25
SmoothAlpha = SmoothStep(RawAlpha)
TargetYawUnwrapped = 连续解包后的目标角
Yaw = StartYaw + (TargetYawUnwrapped - StartYaw) × SmoothAlpha
~~~

所以无论一开始相差 30°、90° 还是 180°，都会在约 0.25 秒结束；即使主角恰好
从 BOSS 正后方跨过 +180° / -180° 边界，也不会让 BOSS 中途反转。

### 13.2 为什么数据放在 NodeMemory

同一个 Behavior Tree 资产可能同时被多个 BOSS 使用。ElapsedTime 和 StartYaw 如果写成
任务 UObject 的普通成员，多只 BOSS 可能共享状态。

NodeMemory 是每个行为树实例自己的内存，因此不会互相串计时。

### 13.3 为什么由任务自己等待硬直和拔剑

“受袭强制入战”是高优先级完整事务。只要 bProvokedByDamage 还为 true，就不应把行为树
让回普通 Boss Alert 或巡逻。

~~~text
同一次命中触发 Staggered
  → 任务保持 InProgress，但不累计转身时间
  → 硬直结束
  → 平滑转身
  → 发送 CombatStart
  → 任务继续保持 InProgress
  → WeaponDrawn 已确认
  → 清 bProvokedByDamage，任务成功
~~~

这样既不会在硬直期间被旧 Boss Alert 瞬间 SetActorRotation，也不会在 DrawSword 刚开始而
bIsBusy 尚未更新的短暂间隙进入 Move To。

Staggered 结束时不能直接沿用进入硬直前的 StartYaw。受击 Montage 可能已经改变角色朝向，
因此任务会把当前 Yaw 重新采样为 StartYaw、把 ElapsedTime 清零，再执行一个完整的
TurnDuration。Executable、BeingExecuted 和 Executed 则属于处决事务/终局状态，任务必须
统一清理并失败，不能像 Staggered 一样无限暂停最高优先级分支。

### 13.4 拔剑完成后的状态交接必须是原子的

`WeaponDrawn` 已确认时，正确顺序是：

~~~text
bPlayerDetected = true
bShouldChase = true
bProvokedByDamage = false   ← 最后释放观察键
~~~

不能先清 `bProvokedByDamage` 再假设旧的 `bShouldChase` 一定仍为 true。导航抑制、任务 Abort
或其它清理可能在拔剑期间改过该键，行为树重新搜索时就会掉进“普通警戒”，形成
“受击转身 → 拔剑 → 再警戒 3 秒 → 战斗”的错误流程。

`Boss.Status.WeaponDrawn` 同时作为“已经完成入战仪式”的持久事实：

- 目标从导航不可达状态恢复时，已拔剑的 BOSS 直接恢复 `bShouldChase=true`；
- 普通 `Boss Alert` 即使因同帧状态切换被短暂选中，检测到已拔剑后也立即成功，不再计时；
- `Boss Damage Alert` 的 Abort 清理若发现目标有效、可达且武器已拔出，会保留战斗交接。

因此首次正常发现且尚未拔剑时仍保留普通警戒；一旦拔剑完成，本次及后续重新发现都直接回到战斗。

---

# 第三部分：阻止旋转与移动竞争

## 14. 修改 BTService_UpdateBossState.cpp

文件：

~~~text
Source/First/Private/AI/BTService_UpdateBossState.cpp
~~~

### 14.1 读取强制入战状态

在写完 bIsBusy 后加入：

~~~cpp
	const bool bProvokedByDamage =
		BB->GetValueAsBool(
			TEXT("bProvokedByDamage"));
~~~

### 14.2 按当前代码结构加入 Frozen 条件

当前工程不是 `if (bIsDead || bIsBusy)` 的旧结构：死亡分支使用 Frozen，而普通 bIsBusy
分支有意使用 FaceTarget，给攻击 Montage、拔剑和 Motion Warping 保留面向目标能力。
不要把 bIsBusy 改成 Frozen。

读取 TargetActor 后，同时取得它的导航抑制状态：

~~~cpp
	ABossAIController* BossController =
		Cast<ABossAIController>(AIController);
	const bool bTargetNavigationBlocked =
		BossController &&
		BossController->IsTargetNavigationBlocked(Target);
~~~

把旋转模式判断开头调整为：

~~~cpp
	if (bIsDead)
	{
		DesiredRotationMode = EBossRotationMode::Frozen;
		AIController->StopMovement();
	}
	else if (bTargetNavigationBlocked)
	{
		// 不可达目标只能等待复查；巡逻移动方向完整接管朝向。
		DesiredRotationMode = EBossRotationMode::OrientToMovement;
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
	}
	else if (bProvokedByDamage)
	{
		DesiredRotationMode = EBossRotationMode::Frozen;
		AIController->StopMovement();
	}
	else if (bIsBusy)
	{
		// 保留当前工程的既有语义：攻击、拔剑、受击和处决 Montage
		// 继续 FaceTarget，让 Motion Warping 与控制器朝向正常工作。
		DesiredRotationMode = EBossRotationMode::FaceTarget;
		AIController->StopMovement();
	}
	else if (IsValid(Target) &&
		bShouldChase &&
		!bPlayerTooFar)
	{
		DesiredRotationMode = EBossRotationMode::FaceTarget;
	}
~~~

完整含义变为：

~~~text
死亡                         → Frozen
目标暂时离开 NavMesh          → OrientToMovement，并强制清 Focus
受伤唤醒/任务手动转身事务      → Frozen
普通 bIsBusy（攻击/拔剑等）    → FaceTarget（保持现有行为）
空闲近距离追击                → FaceTarget
其它空闲状态                  → OrientToMovement
~~~

不要把 bProvokedByDamage 合并进 bIsBusy 的计算。bIsBusy 仍然只表示
ActionBlockingTags 中的真实 GAS 动作；新状态只参与旋转与 StopMovement 判断。

### 14.3 死亡时清理

在 bIsDead 清理区域加入：

~~~cpp
		BB->SetValueAsBool(
			TEXT("bProvokedByDamage"),
			false);
~~~

---

## 15. 修改 UpdateCombatDistance 的目标死亡清理

文件：

~~~text
Source/First/Private/AI/UBTService_UpdateCombatDistance.cpp
~~~

在 bTargetDead 分支现有清理代码中加入：

~~~cpp
		BB->SetValueAsBool(
			TEXT("bProvokedByDamage"),
			false);
~~~

这是第二层保险。目标死亡时，无论先由感知、距离服务还是新任务发现，都不会留下一个永久
为 true 的强制入战键。

文件前面的 `!BossPawn || !Target` 分支不能只清距离键。这里是目标销毁、任务失败和其它
终局流程的共同保险，至少要同时清掉追击、检测状态、Focus 与强制请求。把该分支整理为：

~~~cpp
	if (!IsValid(BossPawn) || !IsValid(Target))
	{
		AIController->StopMovement();
		AIController->ClearFocus(
			EAIFocusPriority::Gameplay);

		BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
		BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
		BB->SetValueAsBool(TEXT("bInAlertRange"), false);
		BB->SetValueAsBool(TEXT("bInAttackRange"), false);
		BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
		BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
		BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
		BB->SetValueAsBool(TEXT("bShouldChase"), false);
		BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
		BB->SetValueAsBool(TEXT("bHasAttacked"), false);
		BB->SetValueAsBool(
			TEXT("bProvokedByDamage"),
			false);
		return;
	}
~~~

这样目标 Actor 被销毁或被其它终局流程主动清空时，不会出现
`TargetActor=null` 但 `bPlayerDetected/bShouldChase/Focus` 仍然有效的半清理状态。

### 15.1 目标离开 NavMesh 时不能保留战斗注视

只保留 `TargetActor` 而仅调用 `ClearFocus()` 并不够。AI Perception 可能持续发送成功感知，
再次调用 `SetFocus()`；与此同时巡逻使用 `OrientToMovement`，两套朝向会表现为“盯玩家—转回
巡逻—再盯玩家”的循环扭头。

当前实现使用 Controller 内的 `NavigationBlockedTarget` 弱引用作为抑制状态：

- 首次确认目标不可达时，取消正在执行的 BOSS 攻击和旧寻路；
- 保留 `TargetActor` 只用于距离服务继续复查；
- 清除 `bPlayerDetected`、全部距离键、`bShouldChase`、`bProvokedByDamage` 与 Focus；
- 后续不可达 Tick 不再反复 `StopMovement()`，否则巡逻 Move To 也会被持续停止；
- 伤害回调和感知成功回调在写 Focus 或 `bProvokedByDamage=true` **之前**同步复查导航状态，
  不能等待下一次距离服务 Tick，否则会先转向一次、再返回巡逻；
- 感知丢失不能清除 `NavigationBlockedTarget`。导航抑制只能由统一导航复查在目标真正返回时解除；
- 感知成功只更新 `TargetActor/bPlayerDetected`，不直接取得 Focus。Gameplay Focus 只由普通警戒、
  追击/近战或受袭任务持有；`OrientToMovement` 的巡逻状态会主动清除残留 Focus；
- 感知成功、普通 Boss Alert、Boss Damage Alert 和 UpdateBossState 都拒绝面向被标记目标；
- 目标回到 NavMesh 后，距离服务主动解除标记并恢复 `bPlayerDetected=true`，不依赖新的感知事件。

不能只用 `ProjectPointToNavigation()` 的返回值判断“人在 NavMesh 上”。该函数只表示在查询盒
内找到了一个可投射导航点；玩家即使已站到网格外，只要离边缘不远也可能返回成功。当前实现还会
比较“玩家水平位置与投射点”的实际 XY 距离。

投射时显式使用 BOSS Controller 的 `NavAgentProperties`，避免在项目存在多套代理尺寸时误用
默认 NavData。当前门控语义是“目标位于 BOSS 对应的导航面附近”；若以后地图加入彼此断开的
导航岛，再在这一入口补充完整路径连通性测试。

投射距离使用迟滞：正常状态默认允许 20 cm 的离开容差；一旦进入不可达状态，必须回到 5 cm
范围内才重新激活。三个参数统一配置在 `BP_BossAIController` 的 `Boss|AI|Navigation` 分类：
`NavMeshExitTolerance`、`NavMeshReentryTolerance` 和
`NavMeshVerticalProjectionExtent`。运行时会把返回容差限制为不大于离开容差，防止蓝图误配后
状态逐 Tick 翻转；行为树距离服务不再保存另一份容易不一致的导航参数。

根节点距离服务在稳定巡逻时不能持续调用 `StopMovement()`。`TargetActor` 为空是巡逻常态，只有
检测到旧战斗键或 Focus 尚未清理的“有战斗 → 无目标”边沿，才停止一次旧寻路并完成清理；否则
每 0.1～0.3 秒都会把巡逻分支刚发起的 `Move To` 中止。

另外，`bProvokedByDamage=true` 只能由 `HandleDamageReceived()` 写入；行为树任务自身只读取它，
并在成功、失败或中止时清为 false。因此在没有新 `DamageTaken` 日志的复现局里，“受袭强制入战”
不会自行反复触发。输出日志中的 `[BossAITrace] Damage alert requested` 才代表一次真实的强制入战请求；
`Navigation blocked/restored` 用于确认导航状态是否在边界抖动。

---

# 第四部分：行为树设置

## 16. 编译 C++ 后重开编辑器

由于本册新增原生 UCLASS：

1. 保存并关闭 Unreal Editor；
2. 使用 Development Editor / Win64 完整编译 First；
3. 编译成功后重新打开项目；
4. 打开 BT_Boss，右键搜索 Boss Damage Alert；
5. 如果搜索不到，先确认类路径、FIRST_API、generated.h 文件名和完整编译是否正确。

不建议只依赖 Live Coding 创建这个新节点。

---

## 17. 在 BT_Boss 新增强制入战分支

### 17.1 优先级位置

在当前负责“死亡 / 战斗 / 警戒 / 巡逻”的主 Selector 下增加一个 Sequence，命名：

~~~text
受袭强制入战
~~~

从左到右的推荐优先级：

~~~text
死亡
→ 受袭强制入战
→ 原有战斗
→ 原有普通警戒
→ 原有巡逻
~~~

死亡始终最高。受伤唤醒必须高于普通战斗、警戒和巡逻，才能立即中止当前 Move To 或
Boss Alert。

### 17.2 给 Sequence 添加装饰器

| 选项 | 值 |
|---|---|
| Blackboard Key | bProvokedByDamage |
| Key Query | Is Set / 已设置 |
| Observer Aborts | Lower Priority / 低优先级 |

这里必须使用 Lower Priority，不建议 Both。

原因是任务完成时会主动把 bProvokedByDamage 清为 false。如果装饰器使用 Both，
黑板变化可能先把当前任务自己中止，再执行 FinishLatentTask；调试器中会表现成任务刚成功
就被 Abort。Lower Priority 仍能在键变为 true 时中止巡逻和普通警戒，但不会在任务主动
清键时自我中止。

不要给这个 Sequence 添加 bIsBusy Is Not Set 装饰器。

Selector 遇到不满足条件的高优先级分支，会继续尝试低优先级分支；它并不会停在原地
“等待”。如果这里用 bIsBusy 门控，同一次命中触发 Staggered 后，行为树可能落回原有
Boss Alert，而 Boss Alert 会直接 SetActorRotation，重新制造瞬转。

短暂硬直和拔剑等待由 Boss Damage Alert 的 TickTask 保持 InProgress，从而让特殊流程
继续占有最高优先级；若进入可处决/正在处决/已处决状态，任务则立即统一清理并失败，
把控制权交给更高优先级的处决或死亡分支。

如果主 Selector 外层已经统一保证 bIsDead=false，不必重复添加死亡装饰器。若没有统一
死亡门控，再给本 Sequence 加 bIsDead Is Not Set、Observer Aborts Both。

### 17.3 添加任务

在“受袭强制入战”Sequence 下添加：

~~~text
Boss Damage Alert
~~~

详情面板设置：

| 参数 | 推荐值 |
|---|---|
| Turn Duration | 0.25 |
| Turn Tolerance Degrees | 2.0 |

这个分支不要再添加：

~~~text
bInAlertRange
bInAttackRange
bPlayerTooFar
bShouldChase
~~~

它处理的是“伤害已经确认了攻击者”，不应再次让视野或距离推翻。

### 17.4 结构示意

~~~text
主 Selector
├─ 死亡
├─ Sequence：受袭强制入战
│    Decorator：bProvokedByDamage Is Set
│               Observer Aborts = Lower Priority
│    └─ Boss Damage Alert
├─ Selector：战斗（保留现有结构）
├─ Sequence：普通警戒（保留 Boss Alert 3 秒）
└─ Sequence：巡逻（保留现有结构）
~~~

---

## 18. 核对现有行为树的中止设置

新分支之外，还要只读核对现有设置：

| 分支 | 条件 | 推荐 Observer Aborts |
|---|---|---|
| 战斗 | bShouldChase Is Set | Both |
| 普通警戒 | bShouldChase Is Not Set 等原条件 | Both |
| 主要移动分支 | bIsBusy Is Not Set | Both |

转身完成时，新任务先写入 bShouldChase=true，但仍用 bProvokedByDamage 保持最高优先级，
直到 WeaponDrawn 已确认。随后它清除 bProvokedByDamage，行为树才会跳过普通 3 秒警戒，
进入现有战斗树。

不要把“受伤唤醒”接到巡逻 Sequence 的末尾。巡逻中的 Move To 可能长时间处于
InProgress，后面的节点根本没有机会运行。

---

# 第五部分：测试与排查

## 19. 分阶段测试

### 19.1 第一阶段：只验证事件和黑板

运行 PIE，打开 BT_Boss 调试器，选择场景中的 BOSS：

1. 让 BOSS 背对主角巡逻；
2. 从背后命中一次；
3. 检查 TargetActor 是否立即变成主角；
4. 检查 bPlayerDetected 是否为 true；
5. 检查 bProvokedByDamage 是否短暂变为 true；
6. 检查巡逻分支是否立刻被中止。

如果 bProvokedByDamage 从未变为 true，先排查事件监听和攻击者 Context，不要先调动画。

### 19.2 第二阶段：只验证转身

暂时观察以下现象：

- Move To 立即停止；
- BOSS 不再沿原巡逻路径滑行；
- BOSS 不会瞬间 Snap；
- 从背对主角到面向主角约用 0.25 秒；
- 179° 到 -179° 的边界不会绕大圈；
- 转身结束时胸口准确面向主角。

### 19.3 第三阶段：验证拔剑和追击

- 未拔剑：转身结束后只播放一次拔剑；
- 拔剑期间不滑步；
- 拔剑完成后进入原有追击/对峙逻辑；
- 已经拔过剑并重新脱战：再次受击只转身，不重复拔剑；
- 正常视觉发现主角：仍然走原来的 3 秒 Boss Alert。

---

## 20. 完整测试矩阵

| 场景 | 预期结果 |
|---|---|
| 巡逻时正面受击 | 很小角度或直接完成转身，然后拔剑追击 |
| 巡逻时背面受击 | 立即停步，0.25 秒平滑转身，拔剑追击 |
| 普通 3 秒警戒中受击 | 中止普通警戒，改走受袭快速入战 |
| 0.25 秒内连续命中 | 只刷新目标，不反复把计时重置为 0 |
| 同一次伤害触发 Staggered | 先完整播放硬直，结束后再平滑转身 |
| Staggered Montage 改变了 BOSS 朝向 | 硬直结束后从当前 Yaw 重新计时，不被旧 StartYaw 拉回 |
| 强制入战中变为 Executable/BeingExecuted/Executed | 立即清理并退出特殊任务，不永久占住最高优先级分支 |
| 已经 bShouldChase=true 时受击 | 不启动特殊转身，不取消现有战斗 Move To |
| 已在攻击中受击 | 不重启特殊转身；现有攻击/受击窗口规则保持不变 |
| 已在拔剑中受击 | 不重复发送 CombatStart |
| 已有 WeaponDrawn、脱战后受击 | 平滑转身后直接追击，不重复拔剑 |
| 主角死亡 | 清理目标和 bProvokedByDamage，不继续追击 |
| 转身/拔剑期间目标丢失且未重现 | 先暂存丢失；拔剑完成且仍在领地内时保留 TargetActor，直接进入战斗 |
| 转身/拔剑期间目标短暂丢失后重现 | 重新感知会撤销延迟记录，任务完成后正常追击 |
| 转身期间 TargetActor 被销毁 | 任务失败；bShouldChase、bPlayerDetected、Focus 与强制请求全部清理 |
| 行为树重启或更高优先级分支 Abort 任务 | AbortTask 走统一清理，不残留 Frozen、Focus 或追击状态 |
| BOSS 死亡 | 立即清理强制入战，不拔剑 |
| BOSS 可处决/正在处决/已处决 | 普通伤害事件不能重新唤醒 |
| 陷阱或无来源伤害 | 掉血，但不把任意 Actor 当成主角 |
| 正常视野发现 | 原有 3 秒视觉警戒完全不变 |
| 主角离开 NavMesh 并继续处于视野内 | BOSS 清 Focus、稳定巡逻，不再循环扭头 |
| 主角站在 NavMesh 边缘 | 进入/返回迟滞阻止状态逐 Tick 翻转 |
| 主角从不可达区域回到 NavMesh | 自动恢复普通警戒，不要求先离开视野再回来 |

---

## 21. 常见问题

### 21.1 受击后掉血，但 bProvokedByDamage 没变化

依次检查：

1. 伤害是否真正进入 DamageTaken 并降低 Health；
2. 本次是否为致命伤害；当前 Shared.Event.DamageReceived 只在非致命伤害时发送；
3. OnPossess 是否执行 BindDamageReceivedEvent；
4. Payload 的 Instigator / OriginalInstigator 是否能追溯到 ADKCharacter；
5. BOSS 是否处于 Dead、Executable、BeingExecuted 或 Executed。

### 21.2 BOSS 瞬间转过去，不是平滑转身

检查：

- 是否误用了原有 Boss Alert，而没有进入 Boss Damage Alert；
- “受袭强制入战”是否真的位于普通警戒之前；
- bProvokedByDamage 装饰器是否能中止低优先级；
- Turn Duration 是否被设成 0；
- 是否还有蓝图 Tick 或其它任务直接调用 SetActorRotation。

### 21.3 BOSS 转身时仍在滑行

检查三层停止是否都存在：

~~~text
AIController::StopMovement
CharacterMovement::StopMovementImmediately
BossRotationMode = Frozen
~~~

同时确认 UpdateBossState 的优先级为：死亡 → 导航不可达 → 受伤强制转身 → 普通忙碌。
导航不可达必须使用 OrientToMovement 并清 Focus；普通 bIsBusy 在目标可达时继续使用
FaceTarget，不能把所有 GAS Montage 一起冻结朝向。

### 21.4 转完不拔剑

检查：

- GA_Boss_DrawSword 是否仍由 Boss.Event.CombatStart 触发；
- BOSS StartupData 是否授予了 GA_Boss_DrawSword；
- DrawSwordMontage 是否配置；
- ASC 是否错误残留 Boss.Status.DrawSword；
- 是否已经存在 Boss.Status.WeaponDrawn；若存在，本来就应跳过拔剑。

### 21.5 拔剑期间又开始移动

检查：

- Boss Damage Alert 是否在确认 WeaponDrawn 前就提前清除了 bProvokedByDamage；
- 受袭 Sequence 是否错误添加了 bIsBusy 装饰器并被中止；
- UpdateBossState 的 ActionBlockingTags 是否包含 Boss.Status.DrawSword；
- 战斗和主要 Move To 分支是否有 bIsBusy Is Not Set；
- 这些 bIsBusy 装饰器的 Observer Aborts 是否为 Both；
- Update Boss State 服务是否挂在始终运行的高层节点。

正确实现后，受袭分支会完整包住“转身 + 拔剑”，不依赖 Update Boss State 下一次服务
Tick 才阻止移动，所以拔剑首帧也不应出现滑步。

### 21.6 受击硬直后不再转身

检查：

- 受袭 Sequence 上不应存在 bIsBusy 装饰器；
- TickTask 在 Staggered 存在时应暂停并记录 bWasTurnPaused，不能清 bProvokedByDamage；
- Staggered 是否在受击 Ability 结束时被正确移除；
- bProvokedByDamage 是否仍为 true，使高优先级任务继续保持 InProgress；
- 硬直结束首帧是否用当前 Actor Yaw 重设 StartYaw，并把 ElapsedTime 清零。

如果卡住的是 Executable、BeingExecuted 或 Executed，则不是等待硬直的问题。这三种状态
应走统一失败清理并让处决/死亡分支接管，不能只 return。

### 21.7 连续攻击让 BOSS 永远转不完

确认 HandleDamageReceived 中：

~~~cpp
BB->GetValueAsBool(TEXT("bProvokedByDamage"))
~~~

已经包含在 bAlreadyEngaged 判断里。第二次命中只能刷新 TargetActor，不能重新把任务计时
清零。

---

## 22. 参数建议

Turn Duration 首版推荐 0.25 秒：

| 数值 | 感受 |
|---|---|
| 0.15～0.20 | 反应很快，压迫感强，但接近瞬转 |
| 0.25 | 本册推荐；能看清转身，又不会让 BOSS 显得迟钝 |
| 0.30～0.40 | 更有重量感，但主角贴身绕背时可能显得反应慢 |

Turn Tolerance Degrees 推荐保持 2°。它只允许已经非常接近目标时提前结束，不会明显改变
整体观感。

Draw Sword Timeout 首版推荐 5 秒，正常拔剑应远短于这个时间；它只用于兜底 Montage
误设循环或 DrawSword 标签异常残留。设为 0 会关闭保护，不建议在正式配置中关闭。

不要用 CharacterMovement 的 RotationRate 代替本参数。RotationRate 是角速度，
不同初始角度会得到不同完成时间；本任务要求固定时间。

---

## 23. 验收标准

全部满足后，本功能才算完成：

- [ ] 巡逻时受到主角非致命伤害会立刻停止原移动；
- [ ] 背面受击能在约 0.25 秒内平滑转向主角；
- [ ] 转身走最短 Yaw，不出现绕大圈；
- [ ] 转身期间不会被移动组件或 Boss Alert 抢旋转；
- [ ] 普通受击硬直存在时会先等硬直结束；
- [ ] 硬直改变朝向后会从当前 Yaw 重新开始转身，不发生回拉；
- [ ] 转到位后才拔剑；
- [ ] 拔剑期间不滑步；
- [ ] 拔剑资源或状态异常时会在等待上限后退出，不永久卡住最高优先级分支；
- [ ] 已拔剑时不会重复拔剑；
- [ ] 受击拔剑完成后直接进入战斗，不会再次执行普通 3 秒警戒；
- [ ] 已拔剑状态下因任务 Abort 或重新进入 NavMesh，也会直接恢复战斗；
- [ ] 连续命中不会无限重置转身；
- [ ] 正常视觉警戒仍保留原来的 3 秒；
- [ ] 强制入战期间的视觉丢失会被暂存，并在任务结束时复核而不是被吞掉；
- [ ] 目标销毁、处决接管或任务 Abort 后不会残留追击键、Focus 或 Frozen；
- [ ] 死亡和处决状态不会被伤害重新拉回战斗；
- [ ] 原有普通受击窗口规则没有被本册改变。

---

## 24. 回退顺序

如果实施后需要快速恢复旧行为，按以下顺序回退：

1. 在 BT_Boss 中断开“受袭强制入战”Sequence；
2. 删除 BB_Boss 的 bProvokedByDamage；
3. 从 UpdateBossState 与 UpdateCombatDistance 移除该键的读取/清理；
4. 从 BossAIController 移除受伤事件绑定、回调和句柄；
5. 最后删除 BTTask_BossDamageAlert.h/.cpp 并完整重新编译。

先断开行为树分支，再删 C++ 类，可以避免 UE 资产暂时持有已不存在的原生节点引用。

---

## 25. 最终职责总结

~~~text
FirstAttributeSet
  负责确认“确实造成了非致命生命伤害”
  并发送 Shared.Event.DamageReceived

BossAIController
  负责识别真正的主角攻击者
  并提出 bProvokedByDamage 请求

BT_Boss
  负责用高优先级分支中止巡逻/普通警戒

BTTask_BossDamageAlert
  负责停止移动、0.25 秒平滑转身、触发拔剑和进入追击

BTService_UpdateBossState
  负责在死亡/特殊事务期间 Frozen；普通 GAS 忙碌继续 FaceTarget

GA_Boss_DrawSword
  继续负责现有拔剑 Montage、武器挂手和 WeaponDrawn
~~~

这套结构把“受到谁的伤害”“现在该执行哪条 AI 分支”“身体如何转动”“拔剑动画如何播放”
分别留在各自负责的系统中，后续扩展远程攻击、多人目标或不同受袭反应时，不需要把所有
规则继续堆进现有 Boss Alert。
