# First

Developed with Unreal Engine 5.6

## 素材恢复说明（全新克隆后必读）

为控制仓库体积，以下商店素材包**不在版本库中**（本地开发目录仍保留）。
全新克隆本仓库后，需从 Fab/商城重新下载并导入到 `Content/` 对应目录，项目才能完整运行：

| 素材包 | 放置目录 | 用途 |
| --- | --- | --- |
| Dark Knight（男/女） | `Content/Dark_Knight/` | 主角模型与贴图 |
| Sword Animset Pro | `Content/SwordAnimsetPro/` | 主角剑术动画 |
| Katana Animations | `Content/Katana_Animations/` | BOSS 动画 |
| Slash Trail (SoftTofu) | `Content/SlashTrail_SoftTofu/` | 武器拖影/剑光特效 |
| Battle Wounds | `Content/Battle_Wounds/` | 受击伤口贴花 |
| Weapons Woosh | `Content/Weapons_woosh/` | 挥砍音效 |
| Rapier AnimSet | `Content/Rapier_AnimSet/` | 备用动画（项目当前未引用） |

其余说明：

- 二进制资产（`.uasset` / `.umap` 等）通过 **Git LFS** 管理，克隆前请确认已安装 git-lfs（`git lfs install`）。
- `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/` 为构建产物，已被忽略，克隆后重新编译即可。
