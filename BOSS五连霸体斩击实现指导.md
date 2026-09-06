# BOSS 五连霸体斩击实现指导

> 招式代号：**FiveCombo（五连斩）**。基于 `UGA_Boss_ThreeCombo` 骨架克隆，
> 新增**霸体窗口**机制：五段挥砍期间 BOSS 不可被普通受击与弹反打断，
> 但**破韧照旧直接打断**、**死亡压过一切**。动画资产由作者本人在编辑器中填充。

---

## 一、设计决策（已拍板）

| # | 决策点 | 口径 |
| --- | --- | --- |
| D1 | 弹反 | 弹反照常生效（玩家侧收益不变），BOSS 韧性照常削减；但五连期间弹反**不触发僵直、不打断招式** |
| D2 | 破韧 | 韧性被打空 → 照旧直接打断五连斩（走现有 Executable 链路） |
| D3 | 死亡 | 血量归零时死亡能力压过霸体，取消五连斩（现有框架天然满足） |
| D4 | 出招条件 | 与三连斩一致：`FBossAttackOption`（距离段/权重/冷却），首版参数照抄三连斩；**独立释放保留**，与接招链并存（低耦合，后续可调） |
| D5 | 普通受击 | 五段挥砍期间不开放受击窗口；**第五段后摇保留 ANS_HitReactWindow 作为玩家惩罚窗口** |
| D6 | 接招链 | 蓄力斩**正常演出完毕**后按概率（首版 50%，可调）延迟（首版 0.5s，可调）接续五连斩；**挥空也接**（跟踪性由五连斩的 Motion Warping 吸附负责）；被打断（破韧/死亡）绝不接 |

### 为什么破韧/死亡不需要写任何新代码

- 破韧：`ABossCharacter::ApplyPoiseDamage` → `Boss_Event_PoiseBroken` → `UGA_Boss_Executable`
  激活时 `CancelAbilities(Boss.Ability.Attack)`（GA_Boss_Executable.cpp:65-72）。
  五连斩的 AssetTags 挂了 `Boss.Ability.Attack` 父标签，自动被取消。→ **D2 免费获得**。
- 死亡：`UGA_Boss_Death` 由 `Shared_Status_Dead` 标签出现自动触发（OwnedTagAdded），
  激活后 `CancelAllAbilities`，且它没有任何 ActivationBlockedTags。→ **D3 免费获得**。
- 结论：霸体只需要拦两条路径——**普通受击**（靠不开受击窗口 + 标签双保险）
  和**弹反僵直**（靠 ParryStagger 内部分支）。

---

## 二、新增 GameplayTag（4 个）

`Source/First/Public/MyGameplayTags.h`（加在 Boss_Status_HitReactWindow 附近）：

```cpp
// BOSS 霸体窗口：五连斩等招式期间存在；期间弹反不触发僵直、受击不打断。
FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Uninterruptible);
// 五连斩身份与冷却。
FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Attack_FiveCombo);
FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Cooldown_Attack_FiveCombo);
```

`Source/First/Private/MyGameplayTags.cpp`：

```cpp
UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Uninterruptible, "Boss.Status.Uninterruptible");
UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Attack_FiveCombo, "Boss.Ability.Attack.FiveCombo");
UE_DEFINE_GAMEPLAY_TAG(Boss_Cooldown_Attack_FiveCombo, "Boss.Cooldown.Attack.FiveCombo");
```

> 命名沿用现有层级：`Boss.Ability.Attack.*` 保证被 Executable/ParryStagger 的
> `CancelAbilities(Boss.Ability.Attack)` 覆盖（这是 D2 破韧打断的前提）。

---

## 三、新增 ANS_SuperArmorWindow（霸体窗口通知）

新文件 `Public/Notifies/ANS_SuperArmorWindow.h` + `Private/Notifies/ANS_SuperArmorWindow.cpp`，
完整照抄 `ANS_HitReactWindow` 的结构（DisplayName 改为 `"Super Armor Window"`）。

与 HitReactWindow 的两点差异（规避上次审查发现的"无计数双重移除"隐患）：

```cpp
// 头文件私有成员：记录本窗口是否真的加过标签。
bool bTagAddedByThisWindow = false;

// NotifyBegin：
//   1. 取 Owner 的 ASC（判空）；
//   2. 死亡门控：HasMatchingGameplayTag(Shared_Status_Dead) 时不加；
//   3. AddLooseGameplayTag(Boss_Status_Uninterruptible, 1) 后 bTagAddedByThisWindow = true。

// NotifyEnd：
//   只有 bTagAddedByThisWindow 为 true 才 RemoveLooseGameplayTag(..., 1)，
//   然后复位标志。蒙太奇被打断时引擎会补发 NotifyEnd（bTriggerOnEnd 默认 true），
//   即使漏发，GA_Boss_FiveCombo::EndAbility 还有兜底清理（见第六节）。
```

---

## 四、修改 GA_Boss_ParryStagger（弹反霸体分支，D1 核心）

`Private/AbilitySystem/Abilities/BOSS/GA_Boss_ParryStagger.cpp::ActivateAbility`，
在 `CommitAbility` 相关判空之后、**现有 CancelAbilities 之前**插入：

```cpp
// 霸体分支：五连斩等霸体招式期间，弹反照常削韧，但不取消招式、不进僵直。
// 注意：不能用 ActivationBlockedTags 拦整个能力——削韧逻辑就在本能力内部，
// 拦掉激活等于弹反不削韧，违反 D1。
const bool bBossUninterruptible =
    ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Uninterruptible);

if (bBossUninterruptible)
{
    const float PoiseDamage = TriggerEventData ? FMath::Max(TriggerEventData->EventMagnitude, 0.f) : 50.f;
    AActor* PoiseSource = TriggerEventData ? const_cast<AActor*>(TriggerEventData->Instigator.Get()) : nullptr;
    const EFirstPoiseDamageResult PoiseResult = Boss->ApplyPoiseDamage(PoiseDamage, PoiseSource);

    // 破韧：Boss_Event_PoiseBroken → Executable 会同步取消包括本能力在内的攻击链
    //（D2 打断路径）。与主流程同样先检查生命周期再退出。
    if (!IsActive() || PoiseResult != EFirstPoiseDamageResult::Reduced)
    {
        FinishParryStagger(false);
        return;
    }

    // 未破韧：静默结束——不取消五连斩、不播僵直蒙太奇、不挂 Staggered。
    FinishParryStagger(false);
    return;
}
```

要点：

1. **不动现有非霸体主流程**（CancelAbilities → ApplyPoiseDamage → 僵直），只在入口分流；
2. 破韧在霸体分支内**照常发生**：`ApplyPoiseDamage` 返回 Broken 时，
   同步激活的 Executable 会取消五连斩——这正是 D2 要的行为；
3. `FinishParryStagger` 已有 `IsActive()` 防御，重复调用安全。

---

## 五、修改 GA_Boss_HitReact（双保险）

构造函数加一行：

```cpp
// 霸体期间即使动画里误放了受击窗口，也不触发受击打断（双保险；
// 正常情况五连斩前五段根本不开放受击窗口）。
ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Uninterruptible);
```

> 因为霸体用的是**动画窗口通知**（不是 GA 全程 OwnedTag），第五段后摇的
> ANS_HitReactWindow 不受影响，惩罚窗口（D5）照常工作。

---

## 六、新增 GA_Boss_FiveCombo（克隆 ThreeCombo）

新文件 `Public/AbilitySystem/Abilities/BOSS/GA_Boss_FiveCombo.h` +
`Private/AbilitySystem/Abilities/BOSS/GA_Boss_FiveCombo.cpp`，
以 `UGA_Boss_ThreeCombo` 为模板，差异如下：

### 构造函数

```cpp
FGameplayTagContainer AssetTags;
AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack);            // 破韧/弹反统一取消的父标签
AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack_FiveCombo);  // 身份标签（BT 按它激活）
SetAssetTags(AssetTags);

ActivationRequiredTags.AddTag(MyGameplayTags::Boss_Status_WeaponDrawn);
ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);
ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_BeingExecuted);
ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);

CooldownGameplayEffectClass = UFirstGE_BossCooldown::StaticClass();
CooldownTags.AddTag(MyGameplayTags::Boss_Cooldown_Attack_FiveCombo);
CooldownDuration = 8.f;   // 首版对齐三连斩（D4），后续在 GA 默认值里调

bIsCancelable = true;     // 注意：这不意味着"可被打断"——
                          // CancelAbilities 只响应破韧/死亡等系统级取消，
                          // 玩家普攻与弹反的中断路径已被第四、五节拦下。
```

### ActivateAbility / EndAbility / HandleMeleeHit

与 ThreeCombo 逐行对应，仅替换数据源：

| ThreeCombo | FiveCombo |
| --- | --- |
| `Boss->GetThreeComboMontage()` | `Boss->GetFiveComboMontage()` |
| `Boss->GetThreeComboDefenseData()` | `Boss->GetFiveComboDefenseData()` |
| `Boss->GetThreeComboDamagePerHit()` | `Boss->GetFiveComboDamagePerHit()` |
| MontageTask 槽名 `"ThreeComboMontage"` | `"FiveComboMontage"` |

`EndAbility` 保持 ThreeCombo 的三件事：`EndAttackWarping()`、
`ToggleWeaponCollision(false)`，并**新增霸体兜底清理**：

```cpp
// 兜底：蒙太奇被打断且引擎漏发 NotifyEnd 时，确保霸体标签不残留。
// SetCount 0 幂等，不会把别处加的计数抹成负数。
if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo())
{
    ASC->SetLooseGameplayTagCount(MyGameplayTags::Boss_Status_Uninterruptible, 0);
}
```

> 沿用 `GA_Boss_RetreatChargedSlash.cpp:253` 对 HitReactWindow 的同款兜底写法，
> 全项目霸体来源只有五连斩一处，SetCount(0) 无误伤面。

---

## 七、修改 ABossCharacter（三组新字段，全部 BP 可调）

`Public/Character/BossCharacter.h`，紧邻三连斩对应字段放置：

```cpp
// 五连斩：单个完整五连蒙太奇（动画本身包含五段挥砍，五个 Section）。
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
TObjectPtr<UAnimMontage> FiveComboMontage;

FORCEINLINE UAnimMontage* GetFiveComboMontage() const { return FiveComboMontage; }

// 五连斩每段基础伤害。
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
float FiveComboDamagePerHit = 20.f;   // 首版略低于三连的 22，五段总量更高

FORCEINLINE float GetFiveComboDamagePerHit() const { return FiveComboDamagePerHit; }

// 五连斩的防御白名单与资源伤害（ParryPoiseDamage 首版 50，对齐其它招式）。
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Defense", meta=(AllowPrivateAccess="true"))
FFirstMeleeDefenseData FiveComboDefenseData;

FORCEINLINE const FFirstMeleeDefenseData& GetFiveComboDefenseData() const { return FiveComboDefenseData; }
```

`Private/Character/BossCharacter.cpp` 构造函数中，对齐现有三招的初始化：

```cpp
FiveComboDefenseData.ParryPoiseDamage = 50.f;
```

---

## 八、行为树与数据资产配置（编辑器操作）

1. **DA_BossStartUpData**：`AbilitiesToGive` 数组追加 `GA_Boss_FiveCombo`；
2. **BT_Boss → BTTask_BossSelectAttack 的 AttackOptions**：复制三连斩那一行，改：
   - `AbilityTag = Boss.Ability.Attack.FiveCombo`
   - `CooldownTag = Boss.Cooldown.Attack.FiveCombo`
   - `MaxRange / MinRange / SelectionWeight`：首版照抄三连斩（D4）；
   - `RequiredRetreatSpace = 0`（五连斩无后撤需求）；
3. **BP_Boss 类默认值**：
   - `FiveComboMontage` ← 作者填充的 `AM_Boss_Attack_FiveCombo`（见第九节）；
   - `FiveComboDamagePerHit`、`FiveComboDefenseData` 按需微调；
   - `GA_Boss_FiveCombo` 的 Motion Warping 参数（`AttackWarpingData`）照抄三连斩首版。

---

## 八-2、接招链：蓄力斩 → 五连斩（D6，已实现）

全部逻辑长在 `UGA_Boss_RetreatChargedSlash` 内部（攻击侧扩展），**行为树资产零改动**——
BT 只通过现有的 `bIsBusy`（来源 `Boss.Status.Attacking`）感知"BOSS 还在忙"。

### 新增的可调参数（GA 类默认值 → Combat|Chain 分组）

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `ChainAttackTag` | `Boss.Ability.Attack.FiveCombo` | 接续招式的身份标签。**蓄力斩不硬编码招式类**，按 Tag 激活；想改接其它招式（或将来的新招）只需换标签，零代码 |
| `ChainAttackChance` | `0.5` | 接招概率 [0,1]；设 0 关闭接招。测试期随手调 |
| `ChainAttackDelay` | `0.5s` | 蓄力斩播完到五连斩启动的延迟 |

### 机制要点

1. **只有正常演出完毕才接**：掷点挂在 `HandleMontageCompleted`；破韧/死亡等打断走
   `HandleMontageInterrupted`，绝不接招——被破韧打断的蓄力斩后面不会蹦出霸体五连；
2. **延迟期间保持忙碌**：掷中概率后立即补一个 `Boss.Status.Attacking` Loose 计数
   （蓄力斩自身 OwnedTag 在 EndAbility 时摘除，Loose 计数把窗口续上），
   BT 的 `bIsBusy` 在整个 0.5s 间隙保持 true，不会抢着选新招/恢复移动；
   定时器回调里无论接续成败都先归还该计数（成功时五连斩自己的 OwnedTag 无缝续上）；
3. **挥空也接**：不做距离检查；接续前只用 `ResolveCurrentTarget` + `IsTargetUsable`
   校验目标存在且未死亡。五连斩的 Motion Warping 吸附负责跟踪性——
   **动画侧需要在五连斩蒙太奇里摆 Warp 窗口**（GA 已支持，参数在 GA 默认值 Combat|Motion Warping）；
4. **接续失败静默放弃**：五连斩独立释放的冷却未转好、或激活条件不满足时，
   `TryActivateAbilitiesByTag` 失败不重试——独立释放路径与接招路径互不干扰；
5. **定时器安全**：`FTimerDelegate::WeakLambda(Boss, ...)` + `TWeakObjectPtr<ASC>`，
   BOSS 销毁后回调自动失效，无悬垂。

---

## 九、动画资产填充清单（作者执行）

新建蒙太奇 `AM_Boss_Attack_FiveCombo`（骨架与现有 BOSS 蒙太奇一致），要求：

1. **五个 Section** 顺序衔接：`FiveCombo1 → FiveCombo2 → FiveCombo3 → FiveCombo4 → FiveCombo5`
   （对齐 `AM_Boss_Attack_Combo` 的三连模式；Section 自动 Next 即可，GA 只播一次）；
2. 每段攻击有效帧放**武器碰撞通知**（照抄三连斩蒙太奇里的摆法：
   开关碰撞 + 发 `DK.Event.MeleeHit` 的 Notify）；
3. **前五段挥砍期间**放 `ANS_SuperArmorWindow`（本次新增，DisplayName "Super Armor Window"）：
   建议从第一段起手帧覆盖到第五段命中帧结束；
4. **第五段后摇**放 `ANS_HitReactWindow`（惩罚窗口，D5）——注意与霸体窗口**不重叠**；
5. 按需摆放 `ANS_SlashTrail`（拖影）与 `AnimNotify_PlaySFX`（音效）；
6. 若某段需要吸附玩家，Motion Warping 窗口照三连斩的间隔摆放（GA 侧共享一个 Warp 会话，已支持多窗口）。

---

## 十、验证清单（编辑器内逐项过）

| # | 场景 | 预期 |
| --- | --- | --- |
| V1 | 五连期间玩家普攻命中 BOSS | 掉血、飘受击特效，但招式不中断、无受击动画 |
| V2 | 五连期间玩家弹反成功（第 1~3 段任意段） | 弹反音效/玩家收益正常；BOSS 韧性条下降；招式继续 |
| V3 | 五连期间弹反把韧性打空 | 五连被立刻打断，进入 Executable 可处决流程（D2） |
| V4 | 五连期间 BOSS 血量被打空 | 立刻中断进入死亡流程（D3） |
| V5 | 第五段后摇内玩家命中 | 触发受击打断（惩罚窗口生效，D5） |
| V6 | 五连结束后检查标签 | `showdebug abilitysystem`：Uninterruptible / Attacking 均已消失 |
| V7 | 冷却 | 打完一轮后 8 秒内 BT 不再选中五连斩 |
| V8 | 五连期间玩家被第 2 段命中且未防御 | 正常掉血/受击（招式对玩家的判定不受霸体影响） |
| V9 | 蓄力斩正常播完（多次采样） | 约 50% 概率在 0.5s 后接出五连斩；`ChainAttackChance` 调 1.0/0.0 时必接/不接 |
| V10 | 蓄力斩挥空（玩家闪避拉开） | 接招照常触发，五连斩通过 Warp 吸附追踪（需动画侧已摆 Warp 窗口） |
| V11 | 蓄力斩被破韧打断 | 绝不接五连斩，正常进入 Executable 流程 |
| V12 | 接招延迟的 0.5s 间隙 | BOSS 不移动、不选新招（bIsBusy 保持 true）；接续失败时行为树立刻恢复调度 |

---

## 十一、已知边界与提醒

1. **顺序假设**：ParryStagger 霸体分支中 `ApplyPoiseDamage` 若破韧，Executable 在同一
   调用栈内同步取消本能力——沿用主流程已验证的 `IsActive()` 检查模式，勿删；
2. **受击事件仍会发送**：五连期间玩家命中仍触发 `Boss_Event_HitReact`（掉血即发），
   只是 HitReact 能力被窗口+标签双重挡住。这是预期行为，不是 bug；
3. 上次审查发现的 `BossCharacter.cpp:383` **Instigator 用错 API** 的 bug 与本功能同链路
   （受击事件），建议顺手修复：改为从属性集伤害事件里取真实攻击者；
4. 霸体窗口是**动画驱动**的：如果作者把窗口摆满整个蒙太奇（含后摇），
   惩罚窗口（D5）会失效——按第九节第 3、4 条摆放。
