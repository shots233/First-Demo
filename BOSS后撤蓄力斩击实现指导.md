# BOSS 后撤蓄力斩击实现指导

> 适用项目：First（UE 5.6 / C++ / GAS / Behavior Tree / Motion Warping）
>
> 文档状态：C++ 已实现并通过 UE 5.6 `FirstEditor Development` 编译；UE 动画、Montage、蓝图、启动数据和行为树资产留给用户手工配置
>
> 技能定位：近距离拉开身位后，以明确的蓄力和白光提示发动一次可格挡、可弹反的前冲重斩
>
> 推荐实现：一个 Gameplay Ability、一个连续 Montage、一个固定目标快照、一个独立武器预警特效

---

## 0. 当前进度（以现有 Montage 截图为准）

以后实际操作只需要按[第 24 节：从当前进度继续操作](#24-从当前进度继续操作)执行；第 1～23 节主要用于理解设计原理和排查问题。

已经完成：

- [x] C++ 功能实现；
- [x] UE 5.6 `FirstEditor Development` 完整编译；
- [x] 后撤动画已重定向为 `Anim_Boss_Retreat`；
- [x] 蓄力斩动画已重定向为 `Anim_Boss_ChargedSlash`；
- [x] 两个动画使用 BOSS 骨架 `SK_DKM_Full`；
- [x] 已创建 `AM_Boss_RetreatChargedSlash`；
- [x] Montage 使用 `DefaultGroup.DefaultSlot`；
- [x] 时间轴只使用两个连续动画块：`Anim_Boss_Retreat → Anim_Boss_ChargedSlash`；
- [x] 两个动画块在约第 45 帧首尾相接，Montage 总长约 2.37 秒；
- [x] 当前只有一个 `Default` Section；
- [x] 预览窗口已经显示 Root Motion 轨迹。

下一步按顺序完成：

- [ ] 把 Montage 的 `Blend In` 从当前 0.25 秒改为 0.05～0.10 秒；
- [ ] 逐帧检查约第 45 帧的姿势衔接和后撤实际距离；
- [ ] 添加 `ChargeBegin`；
- [ ] 添加蓄力阶段的 `ANS_HitReactWindow`；
- [ ] 找到真正开始前冲出刀的帧，再添加 `Commit` 和白光；
- [ ] 添加 Motion Warping、刀光与武器碰撞窗口；
- [ ] 配置 `BP_Boss`、`BP_BossWeapon`、`DA_BossStartUpData` 和 `BT_Boss`；
- [ ] 按第 23 节验收。

当前不需要再创建 Charge、Slash、Recovery 三个独立动画。它们是 `Anim_Boss_ChargedSlash` 内部的三个逻辑时间区间。

---

## 1. 目标

本技能暂定名为“后撤蓄力斩击”，完整表现为：

~~~text
后撤根运动
  → 蓄力并面向主角
  → 武器出现白色亮光和提示音
  → 锁定出刀方向
  → 前冲斩击
  → 攻击后摇
  → 返回正常战斗
~~~

实现后必须满足以下体验目标：

- BOSS 在近距离使用技能，先主动后撤，制造新的交战距离；
- 后撤由动画根运动完成，不使用 Move To 或 Launch Character；
- 蓄力前半段允许 BOSS 面向主角；
- 白光是“攻击承诺点”，必须早于伤害碰撞有效帧；
- 白光出现后不能继续大角度追踪主角；
- 主角在白光后横向闪避时，BOSS 应当允许挥空；
- 主角在 Commit 前超出最大范围时，Slash 仍保留 AttackTarget，但只冲到 650cm 最大前冲边界；
- 前冲终点保留双方胶囊和武器攻击距离，不能贴进主角身体；
- 墙边、悬崖边或 NavMesh 边缘没有安全后撤空间时，不选择本技能；
- 正常结束、受击打断、弹反、死亡和目标失效都不会残留碰撞、白光、方向锁或 AttackTarget。

---

## 2. 本册固定决策

### 2.1 一个 Ability，不拆成三个技能

后撤、蓄力、前冲和后摇属于同一招式的连续事务，统一由：

~~~text
UGA_Boss_RetreatChargedSlash
~~~

管理。

不要拆成“后撤 Ability”“蓄力 Ability”“斩击 Ability”。拆分会增加以下风险：

- 中间 Ability 激活失败，BOSS 卡在半套动作；
- GameplayTag 在三个 Ability 之间交接时出现空档；
- Behavior Tree 在交接帧重新选择移动或其它攻击；
- 冷却、受击取消和死亡清理需要跨多个对象协调；
- Montage 时间和 Gameplay Ability 时间难以保持一致。

### 2.2 一个连续 Montage

推荐资产名：

~~~text
AM_Boss_RetreatChargedSlash
~~~

当前 Montage 实际只使用两个动画块：

~~~text
Anim_Boss_Retreat
  → Anim_Boss_ChargedSlash
~~~

播放过程仍按逻辑分为五个时间区间：

~~~text
BackStep
Charge
Commit
Slash
Recovery
~~~

其中 BackStep 来自 `Anim_Boss_Retreat`；Charge、Commit、Slash 和 Recovery 都位于 `Anim_Boss_ChargedSlash` 内，通过 Montage Notify 标记时间点。它们不是四个额外动画，也不需要创建四个 Montage Section。

首版只保留一个 `Default` Section，让两个动画从头到尾自然连续播放，不使用 `Jump To Section`。

### 2.3 后撤不使用攻击吸附；ChargeBegin 只预热目标

后撤和蓄力区间不能放名为 AttackTarget 的 Motion Warping 窗口。

`ActivateAbility` 中不能调用 `BeginAttackWarping`。后撤结束后的 `ChargeBegin` 可以提前注册并持续更新 `AttackTarget`，但这时仍然不能进入 Motion Warping 窗口。单独存在 WarpTarget 不会推动角色；真正修改根运动的是 Montage 中处于激活状态的 Motion Warping 窗口。

这样安排是为了规避低帧率下的同 Tick 顺序问题：如果 Commit 和 Warp 窗口起点落入同一个动画 Tick，引擎可能先更新 Warp Modifier，因找不到目标而把它永久标记为 Disabled。`ChargeBegin` 预热目标后，Commit 只需再次刷新最终位置并切换为固定目标。

### 2.4 白光必须早于碰撞

白光与伤害碰撞同一帧出现时，玩家没有反应时间，它只能被理解为命中特效。

首版固定为：

~~~text
白光开始
  → 0.12～0.20 秒反应窗口
  → 武器碰撞开启
~~~

蓄力动作负责较早期的大提示；白光与短促音效负责精确告诉玩家“现在应该闪避或弹反”。

### 2.5 后撤和白光后锁方向，蓄力阶段允许瞄准

BackStep：

- 技能激活时按已经通过安全检查的方向冻结自动转向；
- 根运动沿这条固定方向后撤；
- 玩家在短暂后撤过程中移动，不会让 BOSS 的后撤轨迹突然弯向墙边或 NavMesh 外。

Charge：

- 后撤结束的 ChargeBegin 事件暂时解除方向锁；
- BOSS 使用现有 FaceTarget 旋转模式面对主角；
- 玩家绕行会影响 BOSS 最终出刀方向。

ChargeBegin 到白光之前：

- 提前建立动态 AttackTarget；
- 每 0.02 秒只更新目标 Transform；
- 因为此区间没有 Motion Warping 窗口，所以不会产生向前吸附。

白光出现后：

- 记录目标的当前位置；
- 用最新位置覆盖为固定 Motion Warping Transform；
- 停止 0.02 秒动态追踪；
- 冻结 AI Controller 的自动朝向；
- 只允许 Motion Warping 在设定角度内修正出刀方向。

这保证视觉提示与实际攻击方向一致。

---

## 3. 推荐首版节奏

| 阶段 | 推荐时间 | 推荐位移 | 玩家获得的信息 |
|---|---:|---:|---|
| BackStep | 0.35～0.50 秒 | 后撤 180～250 cm | BOSS 正在拉开距离 |
| Charge | 0.50～0.90 秒 | 基本不位移 | 重攻击即将到来 |
| Telegraph | 0.12～0.20 秒 | 不位移 | 精确闪避/弹反提示 |
| Slash | 0.25～0.40 秒 | 前冲 250～400 cm | 攻击已经承诺 |
| Hit Window | 0.10～0.18 秒 | 包含在 Slash 内 | 唯一伤害有效区间 |
| Recovery | 0.60～0.90 秒 | 少量根运动或静止 | 玩家反击窗口 |

建议初始战斗参数：

| 参数 | 首版建议 |
|---|---:|
| CooldownDuration | 10 秒 |
| ChargedSlashDamage | 35 |
| GuardStaminaDamage | 40 |
| ParryPoiseDamage | 50 |
| MinRange | 80 cm |
| MaxRange | 250 cm |
| RequiredRetreatSpace | 220 cm |
| SelectionWeight | 0.65 |
| Motion Warp MaxWarpDistance | 900 cm |
| Motion Warp SurfaceGap | 30 cm |
| Motion Warp MaxWarpTravelDistance | 650 cm |
| Motion Warp bKeepWarpTargetWhenBeyondMaxDistance | true |
| Motion Warp MaxFacingAngle | 30° |
| Motion Warp bTrackTarget | false |

这些是首轮联调参数，不应写死在 Tick 或 AnimNotify 中。

---

## 4. 当前项目中可以直接复用的结构

### 4.1 Ability 生命周期

参考：

- Source/First/Private/AbilitySystem/Abilities/BOSS/GA_Boss_NormalAttack.cpp
- Source/First/Private/AbilitySystem/Abilities/BOSS/GA_Boss_ThreeCombo.cpp

继续复用：

- InstancedPerActor；
- Boss.Ability.Attack 父标签；
- Boss.Status.WeaponDrawn 激活前置；
- Boss.Status.Attacking 全程忙碌状态；
- CommitAbility 应用 Cost 与 Cooldown；
- WaitGameplayEvent 监听 DK.Event.MeleeHit；
- PlayMontageAndWait 管理完成、中断和取消；
- EndAbility 关闭碰撞并清理 Motion Warping；
- ResolveTargetDefense 处理格挡、弹反与正常伤害；
- MakeBossDamageEffectSpecHandle 和 UFirstGE_Damage 结算伤害。

### 4.2 攻击吸附

参考：

- Source/First/Public/Types/FirstCombatTypes.h
- Source/First/Private/Character/BaseCharacter.cpp

当前 BeginAttackWarping 会：

- 保存目标 Actor；
- 每 0.02 秒更新 AttackTarget；
- 按双方胶囊半径与 SurfaceGap 计算止步距离；
- 限制最大吸附距离与最大额外位移；
- 限制相对起手朝向的最大修正角度；
- 在 Ability 结束时由 EndAttackWarping 清理。

本技能仍保留目标有效性、导航连通性、方向角度和胶囊止步距离检查。与普通攻击不同的是，玩家超过 MaxWarpDistance 后不会删除 AttackTarget；目标点会沿 Commit 时的方向截断到 MaxWarpTravelDistance，因此 BOSS 最多前冲 650cm，不能无限追踪。普通攻击和三连击继续保持“超距即不吸附”的保护。

### 4.3 武器碰撞与刀光

参考：

- Source/First/Private/Notifies/FirstANS_WeaponCollision.cpp
- Source/First/Private/Notifies/ANS_SlashTrail.cpp
- Source/First/Private/Items/Weapons/FirstWeaponBase.cpp

继续遵守：

- FirstANS_WeaponCollision 只负责伤害碰撞有效窗口；
- ANS_SlashTrail 只负责斩击拖影；
- 白光预警使用独立组件和独立通知；
- 三种表现不能互相代替。

### 4.4 受击取消

参考：

- Source/First/Private/AbilitySystem/Abilities/BOSS/GA_Boss_HitReact.cpp
- Source/First/Private/Notifies/ANS_HitReactWindow.cpp

当前 BOSS 只有在 Boss.Status.HitReactWindow 存在时才播放受击反应，并按 Boss.Ability.Attack 父标签取消当前攻击。

本技能首版建议：

- Charge 区间允许受击打断；
- 白光出现到斩击结束期间关闭受击窗口；
- Recovery 区间重新开放受击窗口。

因此技能具备清楚的风险交换：

- 玩家贪刀可以在蓄力阶段打断；
- 白光后必须闪避、格挡或弹反；
- BOSS 挥空后的 Recovery 是稳定反击机会。

---

## 5. 文件规划

### 5.1 新增 C++ 文件

~~~text
Source/First/Public/AbilitySystem/Abilities/BOSS/GA_Boss_RetreatChargedSlash.h
Source/First/Private/AbilitySystem/Abilities/BOSS/GA_Boss_RetreatChargedSlash.cpp

Source/First/Public/Notifies/ANS_WeaponTelegraph.h
Source/First/Private/Notifies/ANS_WeaponTelegraph.cpp
~~~

### 5.2 修改 C++ 文件

~~~text
Source/First/Public/MyGameplayTags.h
Source/First/Private/MyGameplayTags.cpp

Source/First/Public/Types/FirstCombatTypes.h
Source/First/Private/Character/BaseCharacter.cpp

Source/First/Public/Character/BossCharacter.h
Source/First/Private/Character/BossCharacter.cpp

Source/First/Public/Items/Weapons/FirstWeaponBase.h
Source/First/Private/Items/Weapons/FirstWeaponBase.cpp

Source/First/Private/AI/BTService_UpdateBossState.cpp

Source/First/Public/AI/Tasks/BTTask_BossSelectAttack.h
Source/First/Private/AI/Tasks/BTTask_BossSelectAttack.cpp
~~~

### 5.3 新增或修改 UE 资产

~~~text
Content/Enemy/AnimBP/Montages/AM_Boss_RetreatChargedSlash
Content/Enemy/BP_Boss
Content/Enemy/Weapon/BP_BossWeapon
Content/Enemy/Data/DA_BossStartUpData
Content/Enemy/AI/BT_Boss
~~~

Niagara 资产名可采用：

~~~text
NS_Boss_WeaponTelegraph_White
~~~

实际目录可以继续放在现有 SlashTrail 或 BOSS 特效目录中。

---

## 6. GameplayTag 设计

在 MyGameplayTags.h 中声明：

~~~cpp
FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(
    Boss_Ability_Attack_RetreatChargedSlash);

FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(
    Boss_Cooldown_Attack_RetreatChargedSlash);

FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(
    Boss_Event_RetreatChargedSlash_ChargeBegin);

FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(
    Boss_Event_RetreatChargedSlash_Commit);

FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(
    Boss_Status_AttackDirectionLocked);
~~~

在 MyGameplayTags.cpp 中定义：

~~~cpp
UE_DEFINE_GAMEPLAY_TAG(
    Boss_Ability_Attack_RetreatChargedSlash,
    "Boss.Ability.Attack.RetreatChargedSlash");

UE_DEFINE_GAMEPLAY_TAG(
    Boss_Cooldown_Attack_RetreatChargedSlash,
    "Boss.Cooldown.Attack.RetreatChargedSlash");

UE_DEFINE_GAMEPLAY_TAG(
    Boss_Event_RetreatChargedSlash_ChargeBegin,
    "Boss.Event.RetreatChargedSlash.ChargeBegin");

UE_DEFINE_GAMEPLAY_TAG(
    Boss_Event_RetreatChargedSlash_Commit,
    "Boss.Event.RetreatChargedSlash.Commit");

UE_DEFINE_GAMEPLAY_TAG(
    Boss_Status_AttackDirectionLocked,
    "Boss.Status.AttackDirectionLocked");
~~~

各标签职责：

| 标签 | 职责 |
|---|---|
| Boss.Ability.Attack | 所有 BOSS 攻击的统一父类别，用于受击、死亡和处决统一取消 |
| Boss.Ability.Attack.RetreatChargedSlash | 本技能的唯一身份标签，供行为树按 Tag 激活 |
| Boss.Cooldown.Attack.RetreatChargedSlash | 本技能冷却存在标记 |
| Boss.Event.RetreatChargedSlash.ChargeBegin | 后撤落地后暂时解除方向锁，进入可瞄准蓄力 |
| Boss.Event.RetreatChargedSlash.Commit | Montage 白光帧通知 Ability 建立固定 AttackTarget |
| Boss.Status.AttackDirectionLocked | 告诉旋转服务不要再用 AI Focus 自动追踪 |

首版不强制增加 Boss.Status.Charging。

原因是 Boss.Status.Attacking 已经可以阻止行为树移动，蓄力是否能被打断可继续由现有 Boss.Status.HitReactWindow 表达。以后需要霸体 UI、特殊音效或二阶段规则时，再增加 Charging 标签。

---

## 7. 为攻击吸附增加“固定快照”模式

### 7.1 修改 FFirstAttackWarpingData

在 FirstCombatTypes.h 的 FFirstAttackWarpingData 中增加：

~~~cpp
// true：像普通攻击一样持续追踪目标；
// false：BeginAttackWarping 时只计算一次目标 Transform。
UPROPERTY(
    EditAnywhere,
    BlueprintReadOnly,
    Category="Motion Warping")
bool bTrackTarget = true;
~~~

默认值必须为 true，确保普通攻击、三连击和主角攻击保持现有行为。

### 7.2 修改 BeginAttackWarping

当前流程是：

~~~text
EndAttackWarping
→ 保存目标和参数
→ UpdateAttackWarping
→ 创建循环 Timer
~~~

修改为：

~~~cpp
UpdateAttackWarping();

if (AttackWarpTargetActor.IsValid() &&
    WarpingData.bTrackTarget)
{
    GetWorldTimerManager().SetTimer(
        AttackWarpUpdateTimerHandle,
        this,
        &ThisClass::UpdateAttackWarping,
        FMath::Max(0.005f, WarpingData.UpdateInterval),
        true);
}
~~~

这样：

- 普通攻击 bTrackTarget=true，行为不变；
- 三连击 bTrackTarget=true，行为不变；
- 后撤蓄力斩基础数据仍为 bTrackTarget=false；
- `ChargeBegin` 临时复制数据并改为 bTrackTarget=true，用于提前准备目标；
- `Commit` 再次使用 bTrackTarget=false 刷新并固定最终 Transform；
- 固定 AttackTarget 会保留到 EndAttackWarping；
- 不需要再创建第二套 Motion Warping 系统。

### 7.3 本技能的 WarpingData

在 GA 默认值或 Ability 蓝图中建议：

~~~text
MaxWarpDistance = 900
SurfaceGap = 30
MaxWarpTravelDistance = 650
bKeepWarpTargetWhenBeyondMaxDistance = true
FacingTurnRate = 720
MaxFacingAngle = 30
UpdateInterval = 0.02
bTrackTarget = false
~~~

这里的 `bTrackTarget=false` 是最终 Commit 数据。代码会在 `ChargeBegin` 临时复制该结构并覆盖为 `true`，到 Commit 再切回 `false`。`bKeepWarpTargetWhenBeyondMaxDistance=true` 表示玩家超过 900cm 后仍会生成有效 AttackTarget，但目标位置最多位于 BOSS 起点前方 650cm；不会原地挥刀，也不会无限追到玩家身边。范围内终点仍会扣除双方胶囊半径和 SurfaceGap。MaxFacingAngle 以白光帧的 BOSS Yaw 为原点；白光之后目标移动不会再次修改 WarpTarget。

---

## 8. ABossCharacter 数据扩展

在 BossCharacter.h 中增加：

~~~cpp
UPROPERTY(
    EditDefaultsOnly,
    BlueprintReadOnly,
    Category="Boss|Anim",
    meta=(AllowPrivateAccess="true"))
TObjectPtr<UAnimMontage> RetreatChargedSlashMontage;

UPROPERTY(
    EditDefaultsOnly,
    BlueprintReadOnly,
    Category="Boss|Combat",
    meta=(AllowPrivateAccess="true", ClampMin="0.0"))
float RetreatChargedSlashDamage = 35.f;

UPROPERTY(
    EditDefaultsOnly,
    BlueprintReadOnly,
    Category="Boss|Combat|Defense",
    meta=(AllowPrivateAccess="true"))
FFirstMeleeDefenseData RetreatChargedSlashDefenseData;

FORCEINLINE UAnimMontage*
GetRetreatChargedSlashMontage() const
{
    return RetreatChargedSlashMontage;
}

FORCEINLINE float
GetRetreatChargedSlashDamage() const
{
    return RetreatChargedSlashDamage;
}

FORCEINLINE const FFirstMeleeDefenseData&
GetRetreatChargedSlashDefenseData() const
{
    return RetreatChargedSlashDefenseData;
}
~~~

在构造函数中给出安全默认值：

~~~cpp
RetreatChargedSlashDefenseData.bBlockable = true;
RetreatChargedSlashDefenseData.bParryable = true;
RetreatChargedSlashDefenseData.GuardStaminaDamage = 40.f;
RetreatChargedSlashDefenseData.ParryPoiseDamage = 50.f;
~~~

最终数值仍允许 BP_Boss 类默认值覆盖。

---

## 9. 后撤落点安全检查

### 9.1 为什么必须检查

根运动能让胶囊遵守碰撞，但它不会自动保证最终位置属于 BOSS 的可行走 NavMesh。

如果没有预检查，可能出现：

- BOSS 后撤到 NavMesh 外；
- BOSS 在悬崖边退下平台；
- BOSS 背靠墙时原地播放完整后撤动作；
- 根运动被墙挡住后，前冲距离与预期完全不匹配；
- 行为树在技能结束后无法恢复正常移动。

### 9.2 建议公共接口

在 ABossCharacter 增加：

~~~cpp
bool HasSafeRetreatSpace(
    float RetreatDistance,
    float NavProjectionTolerance = 50.f,
    float MaxLandingHeightDelta = 60.f) const;
~~~

检查顺序：

1. 将当前 ForwardVector 水平化并归一化；
2. 计算 DesiredLanding = CurrentLocation - Forward * RetreatDistance；
3. 使用 BOSS 胶囊尺寸执行 Sweep By Profile；
4. Sweep 命中阻挡物则返回 false；
5. 使用 UNavigationSystemV1::ProjectPointToNavigation；
6. 查询时传入 BOSS AIController 的 GetNavAgentPropertiesRef；
7. 计算投影点与 DesiredLanding 的 2D 距离；
8. 超过 NavProjectionTolerance 则返回 false；
9. 比较投影点 Z 与 DesiredLanding.Z；
10. 高度差超过 MaxLandingHeightDelta 则返回 false；
11. 从当前 NavAgent 位置到投影落点执行 NavigationRaycast；
12. 导航射线被阻挡则说明直线后撤会跨越 NavMesh 缺口，返回 false；
13. 全部通过后返回 true。

查询必须使用 BOSS 自己的 NavAgent，不能使用默认代理。

碰撞检查建议使用胶囊当前 CollisionProfileName，而不是随意选 Visibility 通道。这样结果更接近 CharacterMovement 真正能否通过。

高度差检查不能省略。只比较 2D 距离时，悬崖下层的 NavMesh 也可能投影成功，随后 BOSS 仍会退下平台。

只检查最终落点也不够。若身后存在窄坑，而坑后同高度平台仍有 NavMesh，终点投影可能成功；NavigationRaycast 用于确认根运动实际经过的整条直线都可行走。

### 9.3 双层保护

安全检查需要同时存在于：

- 行为树选招：避免明知不能使用还反复抽中；
- Ability 的 CanActivateAbility：防止以后从其它入口直接激活时绕过行为树。

Ability 中的检查是最终权威。

---

## 10. 武器白光预警

### 10.1 不复用 SlashTrailComponent

现有 SlashTrailComponent 是持续刀光拖影，时间范围通常覆盖完整挥剑。

白色预警只持续 0.12～0.20 秒，职责不同，必须新增独立组件：

~~~cpp
UPROPERTY(
    EditDefaultsOnly,
    BlueprintReadOnly,
    Category="Weapon|VFX",
    meta=(AllowPrivateAccess="true"))
TObjectPtr<UNiagaraSystem> WeaponTelegraphTemplate;

UPROPERTY(
    VisibleAnywhere,
    BlueprintReadOnly,
    Category="Weapon|VFX",
    meta=(AllowPrivateAccess="true"))
TObjectPtr<UNiagaraComponent> WeaponTelegraphComponent;

UPROPERTY(
    EditDefaultsOnly,
    BlueprintReadOnly,
    Category="Weapon|Audio",
    meta=(AllowPrivateAccess="true"))
TObjectPtr<USoundBase> WeaponTelegraphSound;
~~~

头文件前置声明 USoundBase；播放声音的 cpp 包含 Kismet/GameplayStatics.h。

构造函数中：

~~~cpp
WeaponTelegraphComponent =
    CreateDefaultSubobject<UNiagaraComponent>(
        TEXT("WeaponTelegraphComponent"));

WeaponTelegraphComponent->SetupAttachment(WeaponMesh);
WeaponTelegraphComponent->SetAutoActivate(false);
WeaponTelegraphComponent->SetAutoDestroy(false);
~~~

公共接口：

~~~cpp
UFUNCTION(BlueprintCallable, Category="Weapon|VFX")
void SetWeaponTelegraphEnabled(bool bShouldEnable);
~~~

逻辑与 SetSlashTrailEnabled 类似：

- 启用时按需 SetAsset；
- Activate(true)；
- 关闭时 Deactivate；
- 启用请求遇到 Template 为空时保持关闭并安全返回；
- 关闭请求即使 Template 为空也必须执行 Deactivate；
- 从关闭切换为启用时，若配置了 WeaponTelegraphSound，只播放一次；
- 不创建 Tick。

只在 BP_BossWeapon 配置白光模板。玩家武器不配置即可保持现状。

### 10.2 特效视觉建议

白光需要在不同曝光和背景下都可读：

- 主色为白色，允许少量冷蓝边缘；
- 使用 Unlit / Additive；
- 保持 0.12～0.20 秒，快速起亮、快速衰减；
- 亮度足以触发 Bloom，但不要增加真实 Point Light；
- 同时播放一个短促高频金属提示音；
- 不要只在剑尖放一个很小的点，优先覆盖刀刃主要长度。

如果 Niagara 难以完整覆盖刀身，可在后续增加动态材质 Emissive 脉冲；首版优先使用独立 Niagara，减少材质改造范围。

---

## 11. 新增 ANS_WeaponTelegraph

实现方式参考 ANS_SlashTrail。

职责：

~~~text
NotifyBegin
  → 获取 Mesh Owner
  → 获取 UFirstCombatComponent
  → 获取当前装备武器
  → SetWeaponTelegraphEnabled(true)

NotifyEnd
  → SetWeaponTelegraphEnabled(false)
~~~

类声明建议：

~~~cpp
UCLASS(meta=(DisplayName="Weapon Telegraph"))
class FIRST_API UANS_WeaponTelegraph
    : public UAnimNotifyState
{
    GENERATED_BODY()

public:
    virtual void NotifyBegin(
        USkeletalMeshComponent* MeshComp,
        UAnimSequenceBase* Animation,
        float TotalDuration,
        const FAnimNotifyEventReference&
            EventReference) override;

    virtual void NotifyEnd(
        USkeletalMeshComponent* MeshComp,
        UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference&
            EventReference) override;
};
~~~

该 Notify State 只控制视觉，不负责开启武器碰撞，也不直接结算伤害。

---

## 12. 白光承诺事件

### 12.1 复用现有 Gameplay Event AnimNotify

当前 UAnimNotify_DKGameplayEvent 的实现实际是：

- 获取 Mesh Owner；
- 向 Owner 发送配置的 GameplayTag；
- 不依赖 DKCharacter；
- 对 BOSS 同样有效。

因此首版可以直接在 BOSS Montage 使用它，并分别配置：

~~~text
Boss.Event.RetreatChargedSlash.ChargeBegin
Boss.Event.RetreatChargedSlash.Commit
~~~

为了避免编辑器显示误导，可以只把它的 DisplayName 从：

~~~text
DK Gameplay Event
~~~

调整为：

~~~text
Gameplay Event
~~~

不要直接重命名 UCLASS C++ 类型，避免已有 Montage 资产需要 Core Redirect。

### 12.2 Montage 时间线对齐

BackStep 结束、Charge 开始的第一帧放置：

- Gameplay Event：Boss.Event.RetreatChargedSlash.ChargeBegin。

白光开始帧同时放置：

- Gameplay Event：Boss.Event.RetreatChargedSlash.Commit；
- Weapon Telegraph Notify State 的 Begin。

随后间隔 0.12～0.20 秒，再开始 FirstANS_WeaponCollision。

ChargeBegin 与 Commit Event 是逻辑时序的权威；Telegraph 是表现。即使 Niagara 资产漏配，技能的旋转交接、方向锁和固定 WarpTarget 仍必须正常工作。

---

## 13. UGA_Boss_RetreatChargedSlash 结构

### 13.1 构造函数配置

~~~cpp
UGA_Boss_RetreatChargedSlash::
UGA_Boss_RetreatChargedSlash()
{
    InstancingPolicy =
        EGameplayAbilityInstancingPolicy::
        InstancedPerActor;

    FGameplayTagContainer AssetTags;
    AssetTags.AddTag(
        MyGameplayTags::Boss_Ability_Attack);
    AssetTags.AddTag(
        MyGameplayTags::
        Boss_Ability_Attack_RetreatChargedSlash);
    SetAssetTags(AssetTags);

    ActivationRequiredTags.AddTag(
        MyGameplayTags::Boss_Status_WeaponDrawn);

    ActivationBlockedTags.AddTag(
        MyGameplayTags::Boss_Status_Staggered);
    ActivationBlockedTags.AddTag(
        MyGameplayTags::Shared_Status_Dead);
    ActivationBlockedTags.AddTag(
        MyGameplayTags::Boss_Status_Attacking);
    ActivationBlockedTags.AddTag(
        MyGameplayTags::Boss_Status_DrawSword);
    ActivationBlockedTags.AddTag(
        MyGameplayTags::Boss_Status_Executable);
    ActivationBlockedTags.AddTag(
        MyGameplayTags::Boss_Status_BeingExecuted);
    ActivationBlockedTags.AddTag(
        MyGameplayTags::Boss_Status_Executed);

    ActivationOwnedTags.AddTag(
        MyGameplayTags::Boss_Status_Attacking);

    CooldownGameplayEffectClass =
        UFirstGE_BossCooldown::StaticClass();
    CooldownTags.AddTag(
        MyGameplayTags::
        Boss_Cooldown_Attack_RetreatChargedSlash);
    CooldownDuration = 10.f;

    bIsCancelable = true;
}
~~~

所有 BOSS 攻击都必须保留 Boss.Ability.Attack 父标签，否则受击、死亡、处决和导航阻塞无法统一取消本技能。

### 13.2 推荐成员

~~~cpp
UPROPERTY(
    EditDefaultsOnly,
    Category="Combat|Motion Warping")
FFirstAttackWarpingData AttackWarpingData;

UPROPERTY(
    EditDefaultsOnly,
    Category="Combat|Retreat",
    meta=(ClampMin="0.0", Units="cm"))
float RetreatCheckDistance = 220.f;

UPROPERTY(
    EditDefaultsOnly,
    Category="Combat|Retreat",
    meta=(ClampMin="0.0", Units="cm"))
float RetreatNavTolerance = 50.f;

FGameplayTagContainer CooldownTags;

TWeakObjectPtr<ACharacter>
    CommittedTarget;

bool bCommitReceived = false;
bool bHitResolved = false;
bool bDirectionLockApplied = false;
~~~

需要的方法：

~~~cpp
virtual bool CanActivateAbility(...) const override;
virtual void ActivateAbility(...) override;
virtual void EndAbility(...) override;
virtual const FGameplayTagContainer*
    GetCooldownTags() const override;

UFUNCTION()
void HandleChargeBegin(
    FGameplayEventData Payload);

UFUNCTION()
void HandleAttackCommit(
    FGameplayEventData Payload);

UFUNCTION()
void HandleMeleeHit(
    FGameplayEventData Payload);

UFUNCTION()
void HandleMontageCompleted();

UFUNCTION()
void HandleMontageInterrupted();

void SetAttackDirectionLocked(
    bool bShouldLock);
~~~

HandleMontageCompleted 与 HandleMontageInterrupted 开头都要检查 IsActive()。取消和 Montage 回调可能在相邻时序到达，不能重复调用 EndAbility。

SetAttackDirectionLocked 必须幂等：

- 请求状态与 bDirectionLockApplied 相同则直接返回；
- false→true 时只 AddLooseGameplayTag 一次；
- true→false 时只 RemoveLooseGameplayTag 一次；
- 所有调用都同步更新 bDirectionLockApplied。

这可以防止 Montage 漏放 ChargeBegin 时，Commit 再次添加同一 Loose Tag，最终只移除一次而留下标签计数。

### 13.3 CanActivateAbility

先调用 Super::CanActivateAbility。

随后检查：

- Boss 有效；
- Montage 已配置；
- 当前目标有效且未死亡；
- 目标没有被 BossAIController 标记为 NavigationBlocked；
- HasSafeRetreatSpace 返回 true。

任一失败都返回 false。

不要在 CanActivateAbility 中：

- CommitAbility；
- 修改黑板；
- 添加 GameplayTag；
- 开关特效；
- 启动 Timer；
- 修改 Motion Warping。

它必须保持纯查询。

### 13.4 ActivateAbility

推荐顺序：

~~~text
Super::ActivateAbility
→ 获取 Boss、Montage、ASC 和黑板目标
→ 再次验证
→ CommitAbility
→ 保存 CommittedTarget
→ 重置 bCommitReceived / bDirectionLockApplied
→ SetAttackDirectionLocked(true)，固定安全后撤方向
→ WaitGameplayEvent：DK.Event.MeleeHit
→ WaitGameplayEvent：Boss.Event.RetreatChargedSlash.ChargeBegin
→ WaitGameplayEvent：Boss.Event.RetreatChargedSlash.Commit
→ PlayMontageAndWait
~~~

此处仍然不能调用 `BeginAttackWarping`。目标预热由后撤结束后的 `HandleChargeBegin` 完成。这样 `AttackDirectionLocked` 只负责阻止 AI 自动改变 Yaw，不会让误放在后撤段的 Warp 窗口把 BOSS 拉回玩家。

三个 WaitGameplayEvent 的 UE 5.6 参数顺序为：

~~~cpp
WaitGameplayEvent(
    this,
    EventTag,
    nullptr,
    true,   // OnlyTriggerOnce
    true);  // OnlyMatchExact
~~~

ChargeBegin 与 Commit 都使用 `true, true`。命中事件使用 `false, true`，并由 `bHitResolved` 保证只结算第一个 Commit 后的有效命中。这样即使 Montage 误把一次碰撞通知放在 Commit 之前，早到事件也不会永久消费监听任务；进入真正斩击窗口后仍能正常结算。

### 13.5 HandleChargeBegin

BackStep 结束时执行：

1. 若 bCommitReceived=true，直接忽略晚到或顺序错误的 ChargeBegin；
2. SetAttackDirectionLocked(false)；
3. 确认 bDirectionLockApplied=false；
4. 将旋转模式恢复为 FaceTarget；
5. 如果保存目标仍有效且导航未阻塞，复制 `AttackWarpingData`，临时设置 `bTrackTarget=true`；
6. 调用 `BeginAttackWarping`，提前建立并每 0.02 秒更新 AttackTarget；
7. 恢复 Gameplay Focus；
8. 如果目标无效或导航被阻塞，调用 `EndAttackWarping` 并清除 Focus。

这样 BOSS 只在站稳后的 Charge 阶段重新瞄准和预热目标，后撤路径仍与激活前的安全检查保持一致。注意：Charge 阶段不得放置 AttackTarget Motion Warping 窗口，否则预热目标会让吸附提前生效。

### 13.6 HandleAttackCommit

该函数只允许执行一次：

~~~cpp
if (bCommitReceived)
{
    return;
}
bCommitReceived = true;
~~~

之后：

1. 取得 Boss 与 ASC；
2. SetAttackDirectionLocked(true)；
3. 确认 bDirectionLockApplied=true；
4. 将 BOSS 旋转模式设为 Frozen；
5. 清除 AI Gameplay Focus；
6. 对保存的 CommittedTarget 再次调用 BeginAttackWarping，刷新白光帧的最终位置；
7. AttackWarpingData.bTrackTarget 必须为 false，使 Commit 后的 Transform 固定；
8. 输出 Commit 距离与 `PositionWarp=ClampToMaxTravel` 阶段日志。

如果目标在蓄力期间失效：

- 不切换到新目标；
- 立即调用 EndAttackWarping，停止 ChargeBegin 建立的追踪计时器；
- 以取消状态统一结束 Ability；
- 不允许已经失去目标的 Warp 窗口继续使用旧 Transform。

这是引入 ChargeBegin 预热后的安全清理要求，避免失效目标或旧 Timer 残留到下一次攻击。

如果目标仍有效但已经离开 BOSS 可用 NavMesh，则立即以取消状态结束 Ability。该路径必须走统一 EndAbility 清理，避免 Commit 主动写入 NavigationBlocked 后绕过距离服务的首次取消边沿。

### 13.7 HandleMeleeHit

沿用普通攻击结构：

~~~text
读取 Payload.Target
→ ResolveTargetDefense(
      Target,
      RetreatChargedSlashDefenseData)
→ Damaged 时创建 UFirstGE_Damage
→ 使用 RetreatChargedSlashDamage
→ ApplyEffectSpecHandleToTarget
~~~

本技能只有一个 FirstANS_WeaponCollision 窗口，但命中监听使用 `OnlyTriggerOnce=false`，再由 `bCommitReceived` 与 `bHitResolved` 只结算 Commit 后的第一个有效命中。这样早到的错误碰撞事件不会提前消耗监听任务。

### 13.8 EndAbility

无论正常结束还是取消，都按固定顺序清理：

~~~text
关闭武器碰撞
→ 关闭 Weapon Telegraph
→ 关闭 Slash Trail
→ EndAttackWarping
→ SetAttackDirectionLocked(false)
→ 移除可能残留的 HitReactWindow
→ 清空 CommittedTarget
→ 重置运行时布尔值
→ Super::EndAbility
~~~

不能只依赖 AnimNotify End。

Montage 被受击、死亡或处决打断时，Notify End 的调用时序可能与 Ability 取消交错；Ability 的 EndAbility 必须是最终兜底。

---

## 14. 方向锁与 UpdateBossState

### 14.1 当前冲突

当前 UpdateBossState 在 Boss.Status.Attacking 存在时将 BOSS 视为 Busy，并在目标有效时选择 FaceTarget。

这对普通攻击有利，但会让本技能在白光之后继续被 AI Focus 自动转向，破坏固定攻击方向。

### 14.2 新优先级

读取：

~~~cpp
const bool bAttackDirectionLocked =
    BossASC->HasMatchingGameplayTag(
        MyGameplayTags::
        Boss_Status_AttackDirectionLocked);
~~~

旋转优先级调整为：

~~~text
Dead
→ NavigationBlocked
→ ProvokedByDamage
→ AttackDirectionLocked
→ Busy
→ 近距离 FaceTarget
→ OrientToMovement
~~~

AttackDirectionLocked 分支：

~~~cpp
DesiredRotationMode =
    EBossRotationMode::Frozen;
AIController->StopMovement();
AIController->ClearFocus(
    EAIFocusPriority::Gameplay);
~~~

Frozen 只关闭 CharacterMovement 的自动 Yaw，不会禁止 Montage 根运动或 Motion Warping 修改角色 Transform。

### 14.3 为什么不能只在 Ability 中 ClearFocus

如果 UpdateBossState 不识别方向锁：

- Ability 白光帧 ClearFocus；
- 下一次服务 Tick 看见 Boss.Status.Attacking；
- 服务重新 SetFocus(Target)；
- CharacterMovement 再次朝主角转向。

因此方向锁必须成为服务理解的状态，而不是 Ability 内一次性的操作。

---

## 15. 扩展行为树选招数据

### 15.1 当前限制

当前 FBossAttackOption 只有：

- AbilityTag；
- CooldownTag；
- MaxRange。

选择时对所有可用技能等概率随机。

后撤蓄力斩还需要：

- 最小距离；
- 选择权重；
- 身后安全空间。

### 15.2 新字段

~~~cpp
UPROPERTY(
    EditAnywhere,
    Category="Boss|Attack",
    meta=(ClampMin="0.0"))
float MinRange = 0.f;

UPROPERTY(
    EditAnywhere,
    Category="Boss|Attack",
    meta=(ClampMin="0.0"))
float SelectionWeight = 1.f;

// 0 表示该招式不需要后撤空间检查。
UPROPERTY(
    EditAnywhere,
    Category="Boss|Attack",
    meta=(ClampMin="0.0", Units="cm"))
float RequiredRetreatSpace = 0.f;
~~~

默认值保证已有普通攻击和三连配置不需要立刻重做。

### 15.3 筛选规则

技能加入候选列表前必须满足：

~~~text
AbilityTag 有效
AND 冷却标签不存在
AND Distance >= MinRange
AND Distance <= MaxRange
AND SelectionWeight > 0
AND (
    RequiredRetreatSpace <= 0
    OR Boss.HasSafeRetreatSpace(
        RequiredRetreatSpace)
)
~~~

### 15.4 加权随机

不要继续使用等概率 RandRange(index)。

推荐：

~~~text
累计所有候选技能的 SelectionWeight
→ 在 0～TotalWeight 中随机
→ 按权重依次扣减
→ 第一次 <= 0 的技能被选中
~~~

示例：

| 招式 | Weight |
|---|---:|
| NormalAttack | 1.0 |
| ThreeCombo | 0.8 |
| RetreatChargedSlash | 0.65 |

当三者都可用时，新技能约占 26.5%，不会过度重复。

### 15.5 行为树范围关系

当前 UpdateCombatDistance 的 AttackRadius 默认 250 cm。

首版将本技能 MaxRange 设为 250 cm，与攻击分支入口保持一致。

后撤后距离可能超过 AttackRadius，这是预期行为。技能已由 GAS 接管，Boss.Status.Attacking / bIsBusy 必须继续阻止 Behavior Tree 在技能中途启动追击、侧移或另一招。

不得因为后撤后 bInAttackRange=false 就取消当前 Ability。

---

## 16. Montage 配置

### 16.1 根运动

当前 Montage 已经使用：

~~~text
骨架：SK_DKM_Full
Slot：DefaultGroup.DefaultSlot
第 1 段：Anim_Boss_Retreat（约第 0～45 帧）
第 2 段：Anim_Boss_ChargedSlash（约第 45～142 帧）
总时长：约 2.37 秒
~~~

现在只需要继续确认：

- BackStep 的 Root Bone 确实向后移动；
- Slash 的 Root Bone 确实向前移动；
- Retarget 后根骨位移没有被烘焙到骨盆；
- Animation Sequence 中启用 Root Motion；
- AnimBP 使用 Root Motion from Montages Only；
- 预览时打开 Root Motion 处理，角色胶囊会随动画移动；
- 不使用 LaunchCharacter 叠加第二套位移。

当前预览已经出现 Root Motion 点状轨迹，说明 Montage 能读取位移；仍需单独播放第一段，确认实际后撤距离不超过安全检查使用的 220 cm。若角色仍原地播放，优先检查动画根骨，不要在 Ability 中补 `LaunchCharacter`。

### 16.2 Motion Warping 窗口

只在 Slash 的前冲部分放 Motion Warping Notify State：

~~~text
Warp Target Name = AttackTarget
Warp Translation = true
Ignore Z = true
Warp Rotation = true
Rotation Type = 面向目标
~~~

BackStep、Charge、Telegraph 和 Recovery 不放 AttackTarget 窗口。

### 16.3 推荐通知轨道

根据当前 Montage 左侧已有轨道，建议整理为下面的名称。轨道名称只用于分类，不影响代码：

~~~text
轨道：Gameplay（可把当前名为“1”的轨道重命名）
  BackStep 结束帧：Gameplay Event ChargeBegin
  白光开始帧：Gameplay Event Commit

轨道：Hit React Window
  Charge 区间：ANS_HitReactWindow
  Recovery 区间：ANS_HitReactWindow

轨道：Weapon Telegraph（新增）
  白光区间：ANS_WeaponTelegraph

轨道：Slash Trail
  斩击区间：ANS_SlashTrail

轨道：WeaponCollision
  刀刃真正经过主角的区间：
  FirstANS_WeaponCollision

轨道：Motion Warping
  仅前冲位移区间：
  AttackTarget
~~~

### 16.4 时序不可违反

~~~text
ChargeBegin
  < Commit Begin
  <= Weapon Telegraph Begin
  < Weapon Collision Begin
  < Weapon Collision End
  <= Slash Trail End
  < Recovery HitReactWindow
~~~

白光开始晚于碰撞开始属于配置错误。

---

## 17. BP_Boss 与 BP_BossWeapon 配置

### 17.1 BP_Boss

类默认值中配置：

~~~text
RetreatChargedSlashMontage
RetreatChargedSlashDamage
RetreatChargedSlashDefenseData
~~~

建议首版：

~~~text
Damage = 35
bBlockable = true
bParryable = true
GuardStaminaDamage = 40
ParryPoiseDamage = 50
~~~

### 17.2 BP_BossWeapon

配置：

~~~text
WeaponTelegraphTemplate =
    NS_Boss_WeaponTelegraph_White

WeaponTelegraphSound =
    一次性短促金属提示音
~~~

确认：

- 组件挂在 WeaponMesh；
- 武器从背部切到手部后组件仍跟随；
- 白光不会在出生时自动播放；
- Deactivate 后不会残留粒子；
- SlashTrailTemplate 保持原配置不变。

---

## 18. DA_BossStartUpData

把新 Ability 类加入 ReactiveAbilities：

~~~text
GA_Boss_RetreatChargedSlash
~~~

不要加入 ActivateOnGivenAbilities。

该技能由行为树通过 TryActivateAbilitiesByTag 主动激活，不应该在 BOSS 出生时自动执行。

常见漏配表现：

~~~text
行为树成功选中 AttackTag
→ Boss Activate Ability By Tag 返回 Failed
→ BOSS 不播放 Montage
~~~

此时优先检查 StartUpData 是否真正授予了 Ability。

---

## 19. BT_Boss 配置

在 Boss Select Attack 节点的 AttackOptions 增加：

~~~text
AbilityTag =
  Boss.Ability.Attack.RetreatChargedSlash

CooldownTag =
  Boss.Cooldown.Attack.RetreatChargedSlash

MinRange = 80
MaxRange = 250
SelectionWeight = 0.65
RequiredRetreatSpace = 220
~~~

现有 Boss Activate Ability By Tag 无需为本技能新增专用节点。

该 Task 的 Succeeded 只代表 TryActivateAbilitiesByTag 返回 true，不代表 Montage 已经播放完毕。它会在发起 Ability 后立即结束。

确认行为树忙碌分支满足：

- bIsBusy=true 时不执行 Move To；
- bIsBusy=true 时不再次选择攻击；
- 后撤导致 bInAttackRange=false 时不会中止 GAS Ability；
- Ability 结束后才恢复追击、侧移或下一次选招。

如果攻击 Sequence 后面存在固定 Wait：

- Wait 不能短于完整 Montage；
- 约 2 秒的技能不能继续沿用 1.5 秒等待；
- 更稳妥的结构是让 bIsBusy 作为事务门控；
- 后续可把 Activate Ability Task 升级为监听 AbilityEnded，而不是猜动画时长。

首版优先依靠 Boss.Status.Attacking → bIsBusy 保持技能所有权，固定 Wait 只承担节奏间隔，不能承担 Ability 生命周期。

---

## 20. 格挡与弹反规则

首版建议可格挡、可弹反。

理由：

- 白色闪光容易被玩家理解为精准防御提示；
- 蓄力时间较长，允许玩家通过熟练操作反制；
- 高 GuardStaminaDamage 仍能让普通格挡承受明显代价；
- 弹反继续接入现有 BOSS 韧性系统。

结果：

| 玩家行为 | 结果 |
|---|---|
| 无动作 | 受到 35 基础伤害 |
| 普通格挡且精力足够 | 不掉血，扣 40 精力 |
| 精准弹反 | 不掉血，BOSS 扣 50 韧性 |
| 无敌帧闪避 | Avoided，不结算 |
| 白光后横移脱离刀路 | BOSS 挥空 |

如果以后改为不可格挡，提示颜色应改成红色或加入独立危险符号，不能继续沿用白色弹反语义。

---

## 21. 取消和异常路径

### 21.1 蓄力期间受击

Charge 放置 ANS_HitReactWindow。

主角命中后：

~~~text
Boss.Event.HitReact
→ GA_Boss_HitReact 激活
→ 按 Boss.Ability.Attack 取消本技能
→ 本技能 EndAbility 清理
→ 播放 BOSS 受击 Montage
~~~

### 21.2 白光之后受击

Commit 到 Slash 结束之间不放 HitReactWindow。

普通命中：

- 仍然扣除 BOSS 生命；
- 不播放受击硬直；
- 不取消已承诺斩击。

弹反仍由主角防御系统在武器命中时处理。

### 21.3 Recovery 期间受击

Recovery 放置 ANS_HitReactWindow，允许玩家惩罚挥空或成功闪避后的 BOSS。

### 21.4 死亡、处决与导航阻塞

这些系统都会按 Boss.Ability.Attack 父标签取消攻击。

EndAbility 必须保证：

~~~text
WeaponCollision = false
WeaponTelegraph = false
SlashTrail = false
AttackTarget 不存在
AttackDirectionLocked 不存在
HitReactWindow 不残留
~~~

### 21.5 目标在蓄力时死亡或消失

Commit 前发现目标无效：

- 不重新锁定另一个 Actor；
- 调用 EndAttackWarping 清除 ChargeBegin 预热的 WarpTarget 与 Timer；
- 以取消状态结束 Ability；
- 关闭武器碰撞、白光和拖尾；
- 不继续使用旧目标 Transform 完成前冲。

该行为优先保证技能状态和目标生命周期正确。如果以后希望改成“朝原方向挥空”，需要先显式移除 WarpTarget，再单独设计不依赖 Commit 命中的降级分支。

---

## 22. 调试日志

建议只在阶段边沿输出，不要每 Tick 打印。

推荐：

~~~text
[BossAbilityTrace] RetreatChargedSlash activation rejected |
Reason=NoRetreatSpace

[BossAbilityTrace] RetreatChargedSlash ChargeBegin prepared warp target |
Target=... | Distance=... | PositionWarp=ClampToMaxTravel

[BossAbilityTrace] RetreatChargedSlash Commit received |
Target=... | Distance=... | PositionWarp=ClampToMaxTravel

[BossAbilityTrace] RetreatChargedSlash Commit rejected |
Invalid committed target / Target navigation blocked

[BossAbilityTrace] RetreatChargedSlash ended |
Cancelled=true/false | CommitReceived=true/false
~~~

调试需要重点观察：

- 白光帧是否只收到一次 Commit；
- Commit 前是否没有 AttackTarget；
- Commit 后是否没有循环更新 Timer；
- 白光后 AI Focus 是否被清除；
- EndAbility 是否始终移除方向锁；
- 墙边技能是否在选择阶段被过滤。

---

## 23. 验收测试矩阵

### 23.1 基本流程

- [ ] 开阔地近距离时可以被行为树选中；
- [ ] 后撤由根运动推动胶囊，而不是原地滑步；
- [ ] 后撤期间没有被 AttackTarget 向主角拉回；
- [ ] 后撤期间自动 Yaw 被锁定，实际路径与安全检查方向一致；
- [ ] ChargeBegin 后解除第一次方向锁；
- [ ] 蓄力阶段持续面向主角；
- [ ] 白光和提示音出现在碰撞前；
- [ ] 白光后约 0.12～0.20 秒才进入伤害窗口；
- [ ] 前冲 Motion Warping 只影响 Slash；
- [ ] 主角在 Commit 前跑出 900cm 后，Slash 仍产生位移，但最多前冲 650cm；
- [ ] BOSS 停在主角胶囊外，不穿模；
- [ ] Recovery 结束后正常回到战斗。

### 23.2 方向公平性

- [ ] 玩家在白光前绕行，BOSS 会调整最终方向；
- [ ] 玩家在白光后横移，BOSS 不会大角度拐弯；
- [ ] 玩家在白光后绕到背后，BOSS 允许挥空；
- [ ] Motion Warping 最大角度限制有效；
- [ ] AI Focus 不会在服务 Tick 中重新夺回旋转。

### 23.3 环境安全

- [ ] BOSS 背靠墙时不会选择本技能；
- [ ] BOSS 靠近 NavMesh 边缘时不会选择本技能；
- [ ] BOSS 靠近悬崖时不会后撤出场地；
- [ ] 后撤路径有动态阻挡时 Ability 最终检查会拒绝激活；
- [ ] 失败后行为树可以选择其它技能，不会卡死。

### 23.4 防御

- [ ] 普通格挡扣除正确精力且不掉血；
- [ ] 精准弹反扣除正确 BOSS 韧性；
- [ ] 无敌帧闪避不会结算伤害；
- [ ] 同一碰撞窗口不会对同一目标重复伤害；
- [ ] 挥空时白光仍然出现。

### 23.5 中断清理

- [ ] Charge 中受击会取消技能；
- [ ] Commit 到 Slash 期间普通受击不触发硬直；
- [ ] Recovery 中受击可以取消后摇；
- [ ] 死亡中断后武器碰撞关闭；
- [ ] 处决中断后 AttackTarget 被删除；
- [ ] 任意中断后白光不会常亮；
- [ ] 任意中断后 AttackDirectionLocked 不残留；
- [ ] 任意中断后 BOSS 能恢复正常转向。

### 23.6 冷却与选招

- [ ] 技能进入冷却后不会再次进入候选；
- [ ] 冷却结束后可以重新选择；
- [ ] MinRange 生效；
- [ ] MaxRange 与 AttackRadius 一致；
- [ ] SelectionWeight 不是等概率；
- [ ] 连续测试中不会异常高频重复本技能。

---

## 24. 从当前进度继续操作

本节是当前唯一需要照着执行的操作清单。动画重定向、Montage 创建和两段动画排列已经完成，不再重复这些步骤。

### 24.1 先检查并调整当前 Montage

当前正确结构：

~~~text
AM_Boss_RetreatChargedSlash
Skeleton = SK_DKM_Full
Section = Default
Slot = DefaultGroup.DefaultSlot

约第 0～45 帧：Anim_Boss_Retreat
约第 45～142 帧：Anim_Boss_ChargedSlash
~~~

接下来只做四项检查：

1. 将左侧 `Blend In / 混入时间` 从当前 `0.25` 改为 `0.05～0.10` 秒；`Blend Out / 混出时间` 暂时保留 `0.25` 秒。
2. 把播放头放在两段交界处（约第 45 帧），使用逐帧播放，确认姿势没有突然跳变、旋转或瞬移。
3. 从第 0 帧只播放到第 45 帧，确认第一段确实向后移动，而且实际后撤距离不超过 220 cm。
4. 从第 45 帧继续播放，观察 `Anim_Boss_ChargedSlash` 中三个关键位置：蓄力开始、身体真正开始向前冲、刀刃真正开始挥过目标。后面所有 Notify 都依据这三个画面放置，不按猜测时间放置。

### 24.2 整理当前通知轨道

截图里已经有 `1`、`WeaponCollision`、`Hit React Window`、`Slash Trail` 和 `Motion Warping` 轨道。按下面方式整理即可：

1. 把名为 `1` 的轨道重命名为 `Gameplay`，专门放 `ChargeBegin` 和 `Commit` 两个瞬时事件。
2. 保留 `WeaponCollision`，专门放 `FirstANS_WeaponCollision`。
3. 保留 `Hit React Window`，专门放蓄力和后摇的 `ANS_HitReactWindow`。
4. 保留 `Slash Trail`，专门放 `ANS_SlashTrail`。
5. 保留 `Motion Warping`，专门放前冲区间的 Motion Warping Notify State。
6. 新增一条 `Weapon Telegraph`，专门放 `ANS_WeaponTelegraph` 白光窗口。

轨道名称只是为了让时间轴容易阅读，不参与代码判断。真正影响逻辑的是 Notify 类型、GameplayTag 和起止帧。

### 24.3 按顺序逐个添加通知

不要先一次性添加所有通知。按下面顺序逐个完成，每完成一个就播放预览一次。

#### 第一步：添加 ChargeBegin

1. 把播放头移动到 `Anim_Boss_Retreat` 结束、`Anim_Boss_ChargedSlash` 开始的位置，当前约为第 45 帧。
2. 在 `Gameplay` 轨道该位置右键，选择 `添加通知 / Add Notify → Gameplay Event`。
3. 选中新通知，在右侧详情中设置：

~~~text
Event Tag = Boss.Event.RetreatChargedSlash.ChargeBegin
Montage Tick Type = Branching Point
~~~

这个事件表示“后撤已经完成，现在允许 BOSS 在蓄力阶段重新面向玩家”。它必须位于两段动画交界处，不能放在后撤中间。

#### 第二步：添加蓄力受击窗口

1. 在 `Hit React Window` 轨道添加 `ANS_HitReactWindow`。
2. 起点与 ChargeBegin 对齐。
3. 终点先延伸到“身体准备开始前冲”的前一帧；找到 Commit 后，再把终点精确对齐到 Commit 前一帧。

这段窗口表示蓄力期间可以被玩家正常攻击打断。

#### 第三步：寻找并添加 Commit

1. 逐帧播放 `Anim_Boss_ChargedSlash`，找到身体或根骨真正开始向前移动的第一帧。
2. 把播放头向前退一帧，这一帧就是首版 Commit 位置。
3. 在 `Gameplay` 轨道添加第二个 `Gameplay Event`：

~~~text
Event Tag = Boss.Event.RetreatChargedSlash.Commit
Montage Tick Type = Branching Point
~~~

Commit 后攻击方向会冻结并建立一次固定的 `AttackTarget`。不要把 Commit 放到刀已经碰到玩家之后。

#### 第四步：添加白光

1. 在 `Weapon Telegraph` 轨道添加 `ANS_WeaponTelegraph`。
2. 起点与 Commit 同帧。
3. 长度先设置为 0.12～0.20 秒，终点应早于武器碰撞开始，或者最多与碰撞开始同帧。
4. 时间轴中只保留一个白光窗口，不要重叠两个 `ANS_WeaponTelegraph`。

#### 第五步：添加 Motion Warping

只在前冲斩击真正产生向前 Root Motion 的区间添加 Motion Warping Notify State：

~~~text
Root Motion Modifier = Skew Warp
Warp Target Name = AttackTarget
Warp Translation = true
Ignore Z Axis = true
Warp Rotation = true
Rotation Type = 面向目标
~~~

它的起点不能早于 Commit，终点放在向前位移停止的位置。不要覆盖 `Anim_Boss_Retreat`、蓄力前半段或 Recovery。

#### 第六步：添加刀光和武器碰撞

1. 逐帧找到刀刃开始快速挥动的位置，在 `Slash Trail` 轨道添加 `ANS_SlashTrail`。
2. 逐帧找到刀刃真正经过目标身体的短区间，在 `WeaponCollision` 轨道添加 `FirstANS_WeaponCollision`。
3. 刀光可以比碰撞早 1～2 帧开始、晚 1～2 帧结束。
4. 不要手工添加 `DK.Event.MeleeHit`；武器碰撞系统会自动发送。

#### 第七步：添加后摇受击窗口

碰撞窗口结束之后，在 `Hit React Window` 轨道再添加一个 `ANS_HitReactWindow`，覆盖剩余 Recovery。Commit 到武器碰撞结束之间不要放该窗口。

最终时间顺序必须是：

~~~text
第 0～约 45 帧：Anim_Boss_Retreat
约第 45 帧：ChargeBegin
蓄力区间：Hit React Window
前冲前一帧：Commit + Weapon Telegraph Begin
前冲区间：Motion Warping
挥刀区间：Slash Trail
刀刃经过目标：WeaponCollision
碰撞结束后：Recovery Hit React Window
Montage 结束
~~~

### 24.4 配置 BOSS、武器、启动数据与行为树

1. 打开 `/Game/Enemy/BP_Boss` 的类默认值，在 `Boss|Anim` 中把 `RetreatChargedSlashMontage` 指向新 Montage。
2. 在 `Boss|Combat` 与 `Boss|Combat|Defense` 中确认：`Damage=35`、`bBlockable=true`、`bParryable=true`、`GuardStaminaDamage=40`、`ParryPoiseDamage=50`。
3. 打开 `/Game/Enemy/Weapon/BP_BossWeapon`，给 `WeaponTelegraphTemplate` 指定白色剑身 Niagara；`/Game/SlashTrail_SoftTofu/Niagara/Ice/NS_Sword_Ice` 可用于临时验证，但应先预览颜色与局部轴向。`WeaponTelegraphSound` 只放 0.1～0.2 秒的短提示音；模板为空时白光和提示音都不会触发。
4. 不要改动已有 `SlashTrailTemplate`，白光与拖影是两个独立组件。
5. 打开 `/Game/Enemy/Data/DA_BossStartUpData`，在 `ReactiveAbilities` 数组新增原生类 `GA_Boss_RetreatChargedSlash`；不要加入 `ActivateOnGivenAbilities`。
6. 打开 `/Game/Enemy/AI/BT_Boss`，选中 `Boss Select Attack` Task，在 `AttackOptions` 新增一项：

~~~text
AbilityTag = Boss.Ability.Attack.RetreatChargedSlash
CooldownTag = Boss.Cooldown.Attack.RetreatChargedSlash
MinRange = 80
MaxRange = 250
SelectionWeight = 0.65
RequiredRetreatSpace = 220
~~~

7. `AttackTagKey` 继续使用现有的 `AttackTag`，后面的 `Boss Activate Ability By Tag` 节点无需复制或改线。
8. 保存时只保存上述明确修改的资产；如果 UE 提示批量重导入无关源文件，不要一起导入。

### 24.5 首次联调顺序

1. 暂时把其他 AttackOptions 的 `SelectionWeight` 设为 0、本技能设为 1，保证测试时一定选中；完成测试后恢复原权重并把本技能改回 0.65。
2. 在 80～250 cm、BOSS 已拔剑、身后有 220 cm 平地时触发，确认完整播放一次且 10 秒冷却内不重复选择。
3. 分别在背靠墙、悬崖边、窄坑前测试，确认技能不会被选择。
4. 后撤阶段移动玩家，BOSS 不应在后撤中追踪转身；进入 Charge 后可以重新面向玩家。
5. 白光出现后横向闪避，BOSS 只能进行最多 30 度的小幅修正并允许挥空，不能继续拐弯追人。
6. 在白光前攻击 BOSS，应能打断且白光、刀光、碰撞、方向锁和 `AttackTarget` 都被清理。
7. 在白光后测试格挡和弹反，确认只结算一次命中；Commit 前误触碰撞也不能吃掉真正斩击的命中。
8. 让玩家在蓄力期间离开 BOSS 可用 NavMesh，确认技能统一取消，不会再次触发“巡逻与盯人之间反复扭头”。

代码已经完整编译过。如果 `Gameplay Event`、`ANS_WeaponTelegraph` 或 `GA_Boss_RetreatChargedSlash` 在资产选择器中没有出现，再关闭 UE 并完整构建 `FirstEditor Development Win64`；不要用 Live Coding 补第一次原生类加载。

---

## 25. 常见问题

### 25.1 后撤动画播放但角色不移动

检查：

- 动画根骨是否真的有位移；
- Retarget 是否把根位移烘焙到 Pelvis；
- Animation Sequence 是否启用 Root Motion；
- AnimBP Root Motion Mode 是否为 From Montages Only；
- Montage 是否播放在正确 Slot。

不要第一时间用 LaunchCharacter 补救。

### 25.2 后撤时反而向主角靠近

检查：

- Ability 是否错误地在 ActivateAbility 中调用 BeginAttackWarping；
- BackStep 是否误放 AttackTarget Motion Warping 窗口；
- ChargeBegin 到 Commit 之间是否误放 AttackTarget Motion Warping 窗口；
- 旧 AttackTarget 是否在上一个技能结束时未清理。

`ChargeBegin` 中预热 AttackTarget 本身不会产生移动；只有同一时段存在激活的 Motion Warping 窗口时才会拉动角色。

### 25.3 白光后仍然拐弯追人

检查：

- AttackWarpingData.bTrackTarget 是否为 false；
- Commit 是否只执行一次；
- AttackDirectionLocked 是否添加成功；
- UpdateBossState 是否把方向锁放在 Busy 之前；
- AI Focus 是否被服务重新设置；
- Montage Warp Rotation 是否允许过大的 MaxFacingAngle。

### 25.4 白光出现但没有伤害

白光和伤害是两个独立系统。

检查：

- FirstANS_WeaponCollision 是否存在；
- 当前装备武器是否为 Boss.Weapon.Sword；
- WeaponCollisionBox 是否覆盖刀刃；
- Ability 是否监听 DK.Event.MeleeHit；
- DA_BossStartUpData 是否授予新 Ability；
- DefenseResult 是否为 Avoided、Blocked 或 Parried。

### 25.5 墙边频繁原地播放技能

检查：

- AttackOption.RequiredRetreatSpace 是否为 220；
- Selector 是否调用 HasSafeRetreatSpace；
- Ability CanActivateAbility 是否做第二次检查；
- Sweep 是否使用正确 Capsule Profile；
- Nav projection 是否使用 BOSS NavAgent。

### 25.6 技能结束后 BOSS 不再转向

检查：

- EndAbility 是否移除 Boss.Status.AttackDirectionLocked；
- 所有取消路径是否最终进入 EndAbility；
- UpdateBossState 是否在标签移除后恢复 FaceTarget；
- Gameplay Focus 是否能在下一个服务 Tick 恢复。

### 25.7 Motion Warping 报 Invalid Warp Target

当日志出现：

~~~text
Marking RootMotionModifier as Disabled. Reason: Invalid Warp Target (AttackTarget)
~~~

按顺序检查：

1. 是否先看到 `ChargeBegin prepared warp target`；没有则说明 ChargeBegin Gameplay Event 未收到；
2. 本技能的 `PositionWarp` 是否为 `ClampToMaxTravel`；如果仍为 `DisabledByDistance`，说明运行的不是最新模块或 Ability 默认值覆盖错误；
3. Motion Warping 窗口是否误放在 ChargeBegin 之前；Modifier 首帧找不到目标后不会自动恢复；
4. Warp Target Name 是否严格为 `AttackTarget`，大小写完全一致；
5. Commit 后应出现 `Commit received`，而不是 `Commit rejected`；
6. 修复成功后，`LogMotionWarping` 应出现逐帧 `SkewWarp`，不再出现 Invalid Warp Target。

---

## 26. 回退顺序

如果实现过程中需要快速回退：

1. 先从 BT_Boss AttackOptions 移除新技能；
2. 从 DA_BossStartUpData 移除 Ability；
3. 清空 BP_Boss 的新 Montage 引用；
4. 清空 BP_BossWeapon 的 Telegraph Template；
5. 删除或停用 Montage 中的新 Notify；
6. 再删除新 Ability 和 Notify C++ 类；
7. 移除 BossCharacter、Weapon、Selector 和 UpdateBossState 扩展；
8. 最后移除 GameplayTag 与 bTrackTarget；
9. 关闭 UE 并完整编译。

先解除资产对原生类的引用，再删除 C++ 类，避免 UE 打开资产时引用不存在的节点。

---

## 27. 最终职责

~~~text
BTTask_BossSelectAttack
  负责距离、冷却、权重和后撤空间筛选

UGA_Boss_RetreatChargedSlash
  负责完整技能事务、Commit、伤害与所有结束清理

AM_Boss_RetreatChargedSlash
  负责动作节奏、根运动和所有精确动画窗口

ABossCharacter
  负责技能数据和通用后撤空间查询

ABaseCharacter
  负责固定或动态两种 AttackTarget 更新模式

BTService_UpdateBossState
  负责方向锁存在时禁止 AI Focus 抢旋转

AFirstWeaponBase
  负责独立白光 Niagara 组件

ANS_WeaponTelegraph
  负责白光显示区间

FirstANS_WeaponCollision
  负责唯一伤害碰撞区间

ANS_SlashTrail
  负责斩击拖影

DKDefenseComponent
  继续负责格挡、弹反和无敌帧判定
~~~

这套结构的核心原则是：

~~~text
动画决定时间
GAS 决定事务
行为树决定是否使用
根运动决定后撤
固定 Motion Warping 决定前冲终点
GameplayTag 决定旋转所有权
武器通知决定视觉与碰撞窗口
~~~

只要始终保持“白光之前允许瞄准、白光之后方向承诺”，这招就会兼具压迫感、可读性和公平性。
