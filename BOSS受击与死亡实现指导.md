# BOSS 受击与死亡实现指导

> 适用项目：`First`（UE 5.6 **中文版** / C++ / GAS / 内置 AI）
> 前置条件：主角攻击已经能对 BOSS 造成伤害（能看到 BOSS 血条下降）；BOSS 巡逻/移动已跑通
> 本册目标：主角打 BOSS → 血条下降 → BOSS 播放受击动画并短暂硬直 → 血归零 → 播放死亡动画、停止一切行为

---

## 0. 先看结论（本册完成后得到什么）

```text
主角命中 BOSS
  → BOSS Health 下降（已有链路）
  → ABossCharacter 监听到血量下降 → 向自己发送 Boss.Event.HitReact
  → GA_Boss_HitReact 激活：打断当前动作 → 播放 Anim_DKF_Hit_Fwd → 短暂硬直
  → BOSS 血归零 → AttributeSet 添加 Shared.Status.Dead
  → GA_Boss_Death 自动激活：取消所有能力 → 停止移动 → 播放 Anim_DKF_Death
```

---

## 1. 设计说明（先读）

### 1.1 为什么受击不直接用 DK.Event.MeleeHit 触发

你们项目的 `DK.Event.MeleeHit` 是发给**攻击者（主角）**的，BOSS 收不到。所以 BOSS 受击需要自己的触发：**监听 Health 属性下降**，掉血且未死亡时向自己发 `Boss.Event.HitReact` 事件。这是纯 GAS 做法，以后 Phase 3 削韧也挂在同一条掉血路径上。

### 1.2 死亡用 GAS 原生触发源

`GA_Boss_Death` 用 **`OwnedTagAdded(Shared.Status.Dead)`** 触发：`Shared.Status.Dead` 标签一出现（AttributeSet 已经会加），死亡能力自动激活，不需要行为树轮询。

### 1.3 预留的招式标签

本册新增 `Boss.Ability.Attack` 类别标签（先不实现招式）。以后 BOSS 的轻/重攻击、技能能力都挂这个标签，受击、闪避、格挡取消时按类别一次取消，不用认识具体招式类。

---

## 2. 本册改动总览（按文件找位置）

| 文件/资产 | 路径 | 操作 | 新增/修改内容 |
|---|---|---|---|
| `MyGameplayTags.h/.cpp` | `Source/First/Public/`<br>`Source/First/Private/` | 修改 | 新增 5 个 Boss 标签 |
| `BossCharacter.h/.cpp` | `Public/Character/`<br>`Private/Character/` | 修改 | 新增受击/死亡蒙太奇属性；新增 Health 下降监听 |
| `GA_Boss_HitReact.h/.cpp` | `Public/AbilitySystem/Abilities/Boss/`<br>`Private/AbilitySystem/Abilities/Boss/` | 新建 | 受击反应能力（事件触发） |
| `GA_Boss_Death.h/.cpp` | 同上 | 新建 | 死亡能力（标签触发） |
| `AM_Boss_Hit_Fwd` | `/Game/Enemy/AnimBP/Montages/` | 新建 | 受击蒙太奇（由 `Anim_DKF_Hit_Fwd` 创建） |
| `AM_Boss_Death` | `/Game/Enemy/AnimBP/Montages/` | 新建 | 死亡蒙太奇（由 `Anim_DKF_Death` 创建） |
| `DA_BossStartUpData` | `/Game/Enemy/Data/` | 修改 | ReactiveAbilities 添加两个能力 |
| `BP_Boss` | `/Game/Enemy/` | 修改 | 填 Hit React Montage / Death Montage |

---

## 3. 第一步：新增 Gameplay Tags

> 【改动位置】文件 `MyGameplayTags.h`
> 【新增内容】5 个 Boss 标签声明

在 `Shared_Status_Dead` 声明上方新增：

```cpp
	// BOSS 事件与状态。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_HitReact);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Staggered);
	// BOSS 招式的通用类别（以后轻/重攻击、技能都挂它，便于统一取消）。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Attack);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_HitReact);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Death);
```

> 【改动位置】文件 `MyGameplayTags.cpp`
> 【新增内容】对应定义

在 `Shared_Status_Dead` 定义上方新增：

```cpp
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_HitReact, "Boss.Event.HitReact");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Staggered, "Boss.Status.Staggered");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Attack, "Boss.Ability.Attack");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_HitReact, "Boss.Ability.HitReact");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Death, "Boss.Ability.Death");
```

编译闸门 1：标签声明/定义齐全，能编译。

---

## 4. 第二步：ABossCharacter 新增蒙太奇属性 + Health 下降监听

### 4.1 头文件

> 【改动位置】文件 `BossCharacter.h`
> 【新增内容】两个蒙太奇属性与访问器；`HandleHealthChanged()` 声明；`FOnAttributeChangeData` 前置声明

头文件顶部新增前置声明：

```cpp
struct FOnAttributeChangeData;
```

`public:` 区域新增：

```cpp
	// 受击/死亡蒙太奇，在 BP_Boss 类默认值里配置。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> HitReactMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> DeathMontage;

	FORCEINLINE UAnimMontage* GetHitReactMontage() const { return HitReactMontage; }
	FORCEINLINE UAnimMontage* GetDeathMontage() const { return DeathMontage; }
```

`protected:` 区域新增：

```cpp
	// 血量下降时触发受击事件；死亡（NewValue==0）交给死亡能力处理。
	void HandleHealthChanged(const FOnAttributeChangeData& Data);
```

### 4.2 源文件

> 【改动位置】文件 `BossCharacter.cpp`
> 【新增内容】`BeginPlay` 注册 Health 监听；新增 `HandleHealthChanged()` 实现

新增 include：

```cpp
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffectTypes.h"
#include "MyGameplayTags.h"
```

`BeginPlay()` 里，家点逻辑之后新增：

```cpp
	if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponent())
	{
		// 掉血时触发受击事件；死亡判定在 AttributeSet 里（NewValue==0 时不发）。
		ASC->GetGameplayAttributeValueChangeDelegate(UFirstAttributeSet::GetHealthAttribute())
			.AddUObject(this, &ThisClass::HandleHealthChanged);
	}
```

文件末尾新增：

```cpp
void ABossCharacter::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	// 只有血量下降且还没死透，才触发受击（死亡交给 GA_Boss_Death）。
	if (Data.NewValue < Data.OldValue && Data.NewValue > 0.f)
	{
		FGameplayEventData EventData;
		EventData.Instigator = GetInstigator();

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			this,
			MyGameplayTags::Boss_Event_HitReact,
			EventData);
	}
}
```

编译闸门 2。

---

## 5. 第三步：新建 GA_Boss_HitReact（受击反应能力）

用编辑器向导创建：

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索并选择父类 **`FirstGameplayAbility`**（即 `UFirstGameplayAbility`）；
3. 类名填写 `GA_Boss_HitReact`（不要带 U 前缀，向导会自动加 U，最终类名 `UGA_Boss_HitReact`）；
4. 文件放到 `Source/First/Public/AbilitySystem/Abilities/Boss/` 与 `Private/AbilitySystem/Abilities/Boss/`，整体替换为下面的内容（新建文件）。

> 【改动位置】新建文件 `GA_Boss_HitReact.h` / `GA_Boss_HitReact.cpp`
> 【新增内容】受击反应能力：事件触发、取消招式、播放受击蒙太奇

`GA_Boss_HitReact.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstGameplayAbility.h"
#include "GA_Boss_HitReact.generated.h"

/**
 * BOSS 受击反应能力。
 * 由 ABossCharacter 掉血时发送 Boss.Event.HitReact 触发（AbilityTriggers: GameplayEvent）。
 * 激活时：取消当前招式（按 Boss.Ability.Attack 类别）→ 播放受击蒙太奇 → 结束。
 */
UCLASS()
class FIRST_API UGA_Boss_HitReact : public UFirstGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Boss_HitReact();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	void FinishHitReact(bool bWasCancelled);
};
```

`GA_Boss_HitReact.cpp`：

```cpp
#include "AbilitySystem/Abilities/Boss/GA_Boss_HitReact.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BossCharacter.h"
#include "MyGameplayTags.h"

UGA_Boss_HitReact::UGA_Boss_HitReact()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_HitReact);
	SetAssetTags(AssetTags);

	// 受击期间短暂硬直；不能叠加触发；死亡后不再受击。
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	// 事件触发：ABossCharacter 掉血时发送 Boss.Event.HitReact。
	// 【UE 5.6】旧名 FGameplayAbilityTrigger 已在 5.6 改名为 FAbilityTriggerData。
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::Boss_Event_HitReact;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UGA_Boss_HitReact::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = Cast<ABossCharacter>(GetAvatarActorFromActorInfo());
	if (!Boss)
	{
		FinishHitReact(true);
		return;
	}

	// 打断 BOSS 当前招式（以后 BOSS 攻击能力都要挂 Boss.Ability.Attack 标签）。
	if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackTags;
		AttackTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
		ASC->CancelAbilities(&AttackTags, nullptr, this);
	}

	UAnimMontage* Montage = Boss->GetHitReactMontage();
	if (!Montage)
	{
		FinishHitReact(true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("HitReactMontage"),
			Montage);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UGA_Boss_HitReact::HandleMontageCompleted()
{
	FinishHitReact(false);
}

void UGA_Boss_HitReact::HandleMontageInterrupted()
{
	FinishHitReact(true);
}

void UGA_Boss_HitReact::FinishHitReact(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}
```

编译闸门 3。

---

## 6. 第四步：新建 GA_Boss_Death（死亡能力）

用编辑器向导创建：

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索并选择父类 **`FirstGameplayAbility`**（即 `UFirstGameplayAbility`）；
3. 类名填写 `GA_Boss_Death`（不要带 U 前缀，最终类名 `UGA_Boss_Death`）；
4. 文件放到 `AbilitySystem/Abilities/Boss/`，整体替换为下面的内容（新建文件）。

> 【改动位置】新建文件 `GA_Boss_Death.h` / `GA_Boss_Death.cpp`
> 【新增内容】死亡能力：标签自动触发、取消所有能力、停止移动、播放死亡蒙太奇

`GA_Boss_Death.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstGameplayAbility.h"
#include "GA_Boss_Death.generated.h"

/**
 * BOSS 死亡能力。
 * 由 Shared.Status.Dead 标签添加时自动触发（AbilityTriggers: OwnedTagAdded）。
 * 激活时：取消所有能力 → 停止移动 → 播放死亡蒙太奇 → 结束。
 */
UCLASS()
class FIRST_API UGA_Boss_Death : public UFirstGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Boss_Death();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	void FinishDeath(bool bWasCancelled);
};
```

`GA_Boss_Death.cpp`：

```cpp
#include "AbilitySystem/Abilities/Boss/GA_Boss_Death.h"

#include "AIController.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "Character/BossCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

UGA_Boss_Death::UGA_Boss_Death()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Death);
	SetAssetTags(AssetTags);

	// 死亡标签一出现就自动激活（GAS 原生触发源）。
	// 【UE 5.6】旧名 FGameplayAbilityTrigger 已在 5.6 改名为 FAbilityTriggerData。
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::Shared_Status_Dead;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::OwnedTagAdded;
	AbilityTriggers.Add(Trigger);
}

void UGA_Boss_Death::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = Cast<ABossCharacter>(GetAvatarActorFromActorInfo());
	if (!Boss)
	{
		FinishDeath(true);
		return;
	}

	// 1. 取消所有其它能力（受击、招式等）。
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->CancelAllAbilities(this);
	}

	// 2. 停止移动（角色自身 + AI 控制器）。
	Boss->GetCharacterMovement()->StopMovementImmediately();
	if (AAIController* AIController = Cast<AAIController>(Boss->GetController()))
	{
		AIController->StopMovement();
	}

	// 3. 播放死亡蒙太奇。
	UAnimMontage* Montage = Boss->GetDeathMontage();
	if (!Montage)
	{
		FinishDeath(true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("DeathMontage"),
			Montage);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UGA_Boss_Death::HandleMontageCompleted()
{
	FinishDeath(false);
}

void UGA_Boss_Death::HandleMontageInterrupted()
{
	FinishDeath(true);
}

void UGA_Boss_Death::FinishDeath(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}
```

编译闸门 4。

---

## 7. 第五步：编辑器资产配置

### 7.1 创建受击 / 死亡蒙太奇

> ⚠️ 骨架注意：BP_Boss 当前用的是**男骑士（SK_DKM_Full）**，受击/死亡动画必须用男骑士包里的
> `Anim_DKM_*`，不能沿用女骑士（DKF）动画，否则蒙太奇和 BOSS 骨架不匹配、动画播不出来。

1. 内容浏览器打开 `/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Hit_Fwd`（或 `Anim_DKM_Hit_Alert_Fwd`）；
2. 双击打开动画序列 → 顶部工具栏 **创建（Create）→ 创建蒙太奇（Create Montage）**；
3. 命名 `AM_Boss_Hit_Fwd`，保存到 `/Game/Enemy/AnimBP/Montages/`（文件夹没有就新建）；
4. 对 `Anim_DKM_Death` 重复同样操作，创建 `AM_Boss_Death`；
5. 两个蒙太奇细节面板确认：**插槽（Slot）= `DefaultSlot`**，骨架 = `SK_DKM_Full`。

> 受击/死亡蒙太奇**不需要加碰撞或事件通知**，纯播放即可。

> **ABP 必须接线**：受击/死亡蒙太奇是通过插槽覆盖 Locomotion 播放的，不是状态机里的状态。
> 打开 `ABP_Boss` → AnimGraph，在 `Locomotion` 节点和 `Output Pose` 之间加一个 **插槽（Add Slot）**，
> 命名 `DefaultSlot`，接线为 `Output Pose ← DefaultSlot ← Locomotion`；
> 插槽"播放结束时（When Finished）= 继续状态机（Continue State Machine）"。
> 没有这个插槽，蒙太奇播了也不会显示（输出日志通常会出现 Slot 相关报错或直接看不到动画）。

### 7.1.1 死亡蒙太奇循环收尾（防止播完重新站起来）

**现象**：死亡蒙太奇正常播放完，BOSS 倒地后又站起来，回到站立 Idle。

**原因**：死亡蒙太奇播完就"结束"了，DefaultSlot 按"播放结束时 = 继续状态机"交还给
Locomotion，动画蓝图回到站立姿势。**不是行为树问题，也不是代码问题，纯蒙太奇收尾问题。**

**修复思路**：让死亡蒙太奇**永远不会播完**——把"倒地后的保持姿势"单独切成一段并设循环。
蒙太奇不结束，插槽就不会交还，BOSS 一直保持倒地。

#### 操作步骤（UE 中文编辑器）

1. 内容浏览器打开 **`AM_Boss_Death`**（`/Game/Enemy/AnimBP/Montages/AM_Boss_Death`）；
2. 在蒙太奇时间轴上找到**角色完全倒地之后**的时间点：
   - 拖动播放头到倒地完成的帧，**在分段条（Section 栏）上添加一个分段标记（Section Marker）**；
   - 右键/拖拽标记可以重命名：前段叫 `Fall`（倒地过程），后段叫 `Lying`（倒地保持）；
3. 在左侧**分段（Sections）**列表里选中后段 `Lying`；
4. 细节面板里勾选 **循环（Loop）**；
5. **编译（Compile）并保存（Save）** 蒙太奇；
6. PIE 打死 BOSS 验证：
   - BOSS 倒地后保持姿势，不再站起来；
   - 受击/追击等其他行为不会干扰死亡（行为树已由死亡分支/装饰器停住）。

**注意**：

- ⚠️ **UE 蒙太奇的分段没有"循环勾选框"**：`FCompositeSection` 只有"下一节（Next Section）"字段。
  让最后一段（Lying）永远循环的做法是：在"蒙太奇片段（Sections）"面板里，把 **Lying 的"下一节"设成它自己（Lying）**；
  `Fall` 的"下一节"保持 `Lying`；资产详情里的整体"循环（Loop）"保持**不勾选**（勾了会"倒下去 → 站起来 → 再倒下去"）。
- 如果死亡动画带根运动且倒地时旋转/位移异常，复制动画副本勾"强制根锁定"后再重建蒙太奇；
- 这条只解决"播完站起来"，与"行为树死后不停"是两个独立问题，分开排查。

### 7.2 把两个能力填进 DA_BossStartUpData

1. 打开 `/Game/Enemy/Data/DA_BossStartUpData`；
2. 找到 **ReactiveAbilities** 数组（"预留给 GameplayEvent 等外部事件触发的能力"）；
3. 添加两项：`GA_Boss_HitReact`、`GA_Boss_Death`（原生类，下拉里能搜到）；
4. 保存。

> 若下拉里找不到这两个类，说明没编译或编辑器没刷新：先 Build，再重新打开数据资产。

### 7.3 填 BP_Boss 蒙太奇

打开 `BP_Boss` → **类默认值**：

| 属性 | 值 |
|---|---|
| Boss \| Anim → Hit React Montage | `AM_Boss_Hit_Fwd` |
| Boss \| Anim → Death Montage | `AM_Boss_Death` |

编译并保存。

> 检查：`ABP_Boss` 里必须已经接了 `DefaultSlot`（动画手册做过）；没有的话受击/死亡蒙太奇不会显示。

---

## 8. 编译顺序汇总

```text
闸门 1：MyGameplayTags（新增 5 个 Boss 标签）
闸门 2：ABossCharacter（蒙太奇属性 + Health 下降监听）
闸门 3：GA_Boss_HitReact
闸门 4：GA_Boss_Death
最后：编辑器资产（蒙太奇 / DA_BossStartUpData / BP_Boss）
```

每完成一个闸门做一次普通 Build。

> ✅ 顺序原则：任何类在第一次被引用前，必须已经创建并通过编译。两个 GA 都只依赖
> 已有角色（BossCharacter）与标签，先建 BossCharacter 再建 GA，依赖全部前置。

---

## 9. 测试清单

| 测试 | 操作 | 预期结果 |
|---|---|---|
| 掉血 | 主角攻击 BOSS | BOSS 血条下降 |
| 受击动画 | 单次命中 | BOSS 播放 `AM_Boss_Hit_Fwd`，短暂停顿后恢复 |
| 连续受击 | 快速连打 | 每次命中都有受击表现，不死锁（硬直期间不叠加） |
| 死亡触发 | 把 BOSS 血打到 0 | 播放 `AM_Boss_Death`，BOSS 停止移动 |
| 死后不再受击 | 尸体上继续攻击 | 不再播放受击动画 |
| 死亡时无其它行为 | 攻击中/移动中被打死 | 招式/移动被终止，只播死亡 |
| 重跑 | 重启 PIE | 每次都能正常受击、死亡，不残留状态 |

---

## 10. 常见问题与排查

| 现象 | 原因 | 修复 |
|---|---|---|
| 掉血但不播放受击 | Health 监听没注册，或事件标签不一致，或能力没授予 | 检查 4.2 的 `AddUObject`、标签名 `Boss.Event.HitReact`、7.2 的 ReactiveAbilities |
| 打不到 BOSS（日志无 `DamageTaken`） | 碰撞设置误放到了 **Mesh（CharacterMesh0）** 上，而不是 **CapsuleComponent**；或 BOSS 胶囊体对 `WorldDynamic` 还是默认的 Block | 碰撞配置只放 **CapsuleComponent**：生成重叠事件勾选、对象类型 Pawn、`WorldDynamic = 重叠`、`Pawn = 阻挡`、`WorldStatic = 阻挡`；**Mesh 恢复 NoCollision、取消生成重叠事件**；地板移动性必须为静态 |
| 受击播放不了动画 | 蒙太奇没填 / ABP 没有 DefaultSlot | 检查 7.3；确认 `ABP_Boss` 动画图里有 `DefaultSlot` |
| 死亡不触发 | GA_Boss_Death 没授予，或触发源没选 OwnedTagAdded | 检查 7.2；确认 6 的构造函数 TriggerSource |
| 死亡动画播完重新站起来 | 死亡蒙太奇播完 blend 回 Locomotion（站立 Idle） | 打开 `AM_Boss_Death`，在"倒地完成"处加分段标记，把后段（保持姿势）单独设**循环（Loop）**；蒙太奇永不结束 → 插槽不会交还给 Locomotion |
| 死亡后还播放受击 | HitReact 的 BlockedTags 没加 Dead | 检查 5 构造函数：`ActivationBlockedTags` 加 `Shared.Status.Dead` |
| 连续受击疯狂抽搐 | 受击之间没有间隔 | 现状由 `Boss.Status.Staggered` 阻塞解决；不够就再给 HitReact 加冷却（Cooldown） |
| 编译报找不到 `GetHitReactMontage` | GA 比 BossCharacter 先编译 | 按第 8 节顺序：先闸门 2 再闸门 3/4 |
| 编译报 C2065：`FGameplayAbilityTrigger` 未声明的标识符 | UE 5.6 把该结构体改名为 `FAbilityTriggerData`（旧名来自 5.5 及更早），枚举 `EGameplayAbilityTriggerSource` 没变 | 把构造函数里的 `FGameplayAbilityTrigger` 改成 `FAbilityTriggerData`；`Abilities/GameplayAbilityTypes.h` 的 include 保留即可 |
| 找不到 `ReactiveAbilities` | 数组名记错 | 属性名就是 `ReactiveAbilities`（DataAsset_StartUpDataBase） |

---

## 11. 后续扩展

1. **四方向受击**：包里有 `Anim_DKF_Hit_Left/Right/Bwd`，用攻击者与 BOSS 的角度差选方向（参考 Warrior 的 `ComputeHitReactDirectionTag`）；
2. **韧性接入（Phase 3）**：Poise 削减后，按决策书 D13/D14 决定"是否所有攻击都打断、普通受击是否削韧"；大僵直走独立表现；
3. **死亡细节（决策书 D20/D21）**：死亡动画播完后关闭碰撞、可选掉落物；
4. **处决（Phase 3）**：韧性归零 → `Boss.Status.Executable` → 处决演出；
5. **BOSS 攻击**：下一步实现持剑三连与招式，所有招式能力挂 `Boss.Ability.Attack` 标签。
