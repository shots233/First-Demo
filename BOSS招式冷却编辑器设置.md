# 在 Boss Select Attack 中调整招式冷却

现在可以在行为树的每个招式选项中，像调整 Min Range / Max Range 一样直接查看和调整冷却秒数。打开旧行为树时，尚未填写的冷却会自动填入该技能原来的真实秒数；你已经填写的 `0` 或正数会保留。

本次直接显示秒数的改动已完成 FirstEditor 完整编译，5 项冷却检查全部通过，其中 2 项新增检查覆盖自动填值及保留手动设置。还只读加载了你保存的 BT_Boss，确认四个条目的秒数分别为：普通攻击 4、三连击 8、后撤蓄力斩 10、五连斩 8。报告：[BossAttackCooldownDisplay](D:/UE2026/First/Saved/Automation/BossAttackCooldownDisplay/index.json)。

此前冷却功能接入时的 30 项检查也已通过，包含实际冷却到期、反击和追击。相关日志中有项目既有的启动诊断及初始化、导航等警告；本次新增检查没有失败。此前报告：[BossAttackTableCooldown](D:/UE2026/First/Saved/Automation/BossAttackTableCooldown/index.json)。

## 编辑器操作

1. 使用本次完整编译后的项目重新打开 UE。
2. 内容浏览器进入 **Content → Enemy → AI**，双击 **BT_Boss**。
3. 找到攻击分支下已有的紫色任务 **Boss Select Attack**，单击选中它。
4. 在右侧 **细节 / Details** 面板展开 **Boss → Attack → Attack Options**。如果右侧没有细节面板，通过顶部 **窗口 / Window → Details** 打开。
5. 展开数组中的 **0、1、2……**。先看每项的 **Ability Tag**，确定它对应普通攻击、三连击、五连斩还是后撤蓄力斩。
6. 找到 **Cooldown Duration**。这里直接显示秒数，例如普通攻击原值为 `4`、三连击原值为 `8`；你手动改过的数值以你的设置为准。它和 **Cooldown Tag、Max Range、Min Range** 位于同一个招式条目中。
7. 保存 **BT_Boss**，再开始游戏测试。现有节点会自动显示新字段，不需要删除重建，也不需要添加黑板变量。

| Cooldown Duration | 含义 |
|---|---|
| `0` | 此招式不产生新的招式冷却 |
| `10` | 此招式成功开始时，进入 10 秒冷却 |

例如想降低三连击出现的频率：找到 `Boss.Ability.Attack.ThreeCombo` 对应条目，将 **Cooldown Duration** 从原来的 `8` 改为 `12`，其它条目先保持原样。这是调参示例，没有替你调整战斗数值。

以后在新的数组条目中选择 **Ability Tag** 时，会自动填入该招式的原冷却。更换已有条目的招式身份，也只会重新填入这一行的冷却；单纯编辑冷却秒数不会触发重置。

填入后，行为树中保存的秒数就是这张选招表使用的冷却，不再跟随技能类默认值自动变化。今后集中在这里调参即可。

**Cooldown Tag 仍保留原来的对应标签。** 它负责识别“这个招式还在冷却”，Duration 负责冷却多久。不要把标签改成别的招式，也不要清空。

## 招式冷却与攻击间隔的区别

- **Boss Select Attack → Cooldown Duration**：限制某一招多久之后可以再次使用，技能提交成功时开始计时。仅选中招式或发动条件失败不会消耗冷却。
- **Boss Attack Cooldown → Recovery Time Min / Max**：使用原有“攻击结束后休息”模式时，控制整个攻击过程结束后的休息时间，例如 3～6 秒。

举例：三连击冷却设为 12 秒，动画在第 2 秒结束，本次休息抽到 4 秒。第 6 秒起，BOSS 有资格按行为树选择其它已经冷却好的招式；三连击要到第 12 秒才有资格再次被选中。距离、权重、周旋和后退等条件也仍然参与决定，并不保证一到时间就立即攻击。

受到足够次数攻击触发的反击，可以跳过全局休息，但仍不能使用处于招式冷却中的技能。将某一招设为 `0` 也不会关闭全局休息或允许它在正在攻击时重复发动。

## 生效范围

- 每次执行选招节点时，它的整张招式表会应用到当前 BOSS；因此表内五连斩即使是由后撤斩接续发动，也会使用这里设置的冷却。
- 前闪追击仍使用 **BP_Boss → Pursuit Settings → Cooldown**，不受普通选招表影响。
- 修改影响后续新发动的冷却，已经在倒计时的冷却不会被清除或缩短。调参对比时建议停止游戏、改值保存，再重新开始。
- 同一招式建议只保留一个条目。若重复条目的冷却数值相互冲突，该招式会回退到技能默认值。
- 如果以后使用多张 Boss Select Attack 选招表，以最近执行的节点整表为准；当前表未列出的普通招式恢复使用技能默认冷却，不会遗留上一张表的值。

自动填值从 **Default Cooldown Source** 指向的出生数据读取，目前默认就是你 BOSS 使用的 **DA_BossStartUpData**。它是节点细节中的高级选项，现有 BOSS 无需设置。如果以后换用另一份出生数据，应让这个来源与新 BOSS 一致。来源里没有对应技能或同一个标签对应多个技能时，节点会显示 `Unresolved cooldown` 提示；此时检查来源，或直接填入所需秒数。旧版负数只作为未解析资源的兼容回退，不再是日常调参方式。

底层仍使用项目原有的技能冷却效果，只将行为树提供的秒数传入它；并没有再增加一套独立计时器。引擎参数机制可参考 [Epic：SetSetByCallerMagnitude](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/GameplayAbilities/FGameplayEffectSpec/SetSetByCallerMagnitude)。
