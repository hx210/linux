// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/mtk_scp.h>
#include <media/videobuf2-dma-contig.h>

#include "mtk-mdp3-core.h"
#include "mtk-mdp3-cfg.h"
#include "mtk-mdp3-m2m.h"

static const struct of_device_id mdp_of_ids[] = {
	{ .compatible = "mediatek,mt8183-mdp3-rdma",
	  .data = &mt8183_mdp_driver_data,
	},
	{ .compatible = "mediatek,mt8188-mdp3-rdma",
	  .data = &mt8188_mdp_driver_data,
	},
	{ .compatible = "mediatek,mt8195-mdp3-rdma",
	  .data = &mt8195_mdp_driver_data,
	},
	{ .compatible = "mediatek,mt8195-mdp3-wrot",
	  .data = &mt8195_mdp_driver_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mdp_of_ids);

static struct platform_device *__get_pdev_by_id(struct platform_device *pdev,
						struct platform_device *from,
						enum mdp_infra_id id)
{
	struct device_node *node, *f = NULL;
	struct platform_device *mdp_pdev = NULL;
	const struct mtk_mdp_driver_data *mdp_data;
	const char *compat;

	if (!pdev)
		return NULL;

	if (id < MDP_INFRA_MMSYS || id >= MDP_INFRA_MAX) {
		dev_err(&pdev->dev, "Illegal infra id %d\n", id);
		return NULL;
	}

	mdp_data = of_device_get_match_data(&pdev->dev);
	if (!mdp_data) {
		dev_err(&pdev->dev, "have no driver data to find node\n");
		return NULL;
	}

	compat = mdp_data->mdp_probe_infra[id].compatible;
	if (strlen(compat) == 0)
		return NULL;

	if (from)
		f = from->dev.of_node;
	node = of_find_compatible_node(f, NULL, compat);
	if (WARN_ON(!node)) {
		dev_err(&pdev->dev, "find node from id %d failed\n", id);
		return NULL;
	}

	mdp_pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (WARN_ON(!mdp_pdev)) {
		dev_err(&pdev->dev, "find pdev from id %d failed\n", id);
		return NULL;
	}

	return mdp_pdev;
}

int mdp_vpu_get_locked(struct mdp_dev *mdp)
{
	int ret = 0;

	if (mdp->vpu_count++ == 0) {
		ret = rproc_boot(mdp->rproc_handle);
		if (ret) {
			dev_err(&mdp->pdev->dev,
				"vpu_load_firmware failed %d\n", ret);
			goto err_load_vpu;
		}
		ret = mdp_vpu_register(mdp);
		if (ret) {
			dev_err(&mdp->pdev->dev,
				"mdp_vpu register failed %d\n", ret);
			goto err_reg_vpu;
		}
		ret = mdp_vpu_dev_init(&mdp->vpu, mdp->scp, &mdp->vpu_lock);
		if (ret) {
			dev_err(&mdp->pdev->dev,
				"mdp_vpu device init failed %d\n", ret);
			goto err_init_vpu;
		}
	}
	return 0;

err_init_vpu:
	mdp_vpu_unregister(mdp);
err_reg_vpu:
err_load_vpu:
	mdp->vpu_count--;
	return ret;
}

void mdp_vpu_put_locked(struct mdp_dev *mdp)
{
	if (--mdp->vpu_count == 0) {
		mdp_vpu_dev_deinit(&mdp->vpu);
		mdp_vpu_unregister(mdp);
	}
}

void mdp_video_device_release(struct video_device *vdev)
{
	struct mdp_dev *mdp = (struct mdp_dev *)video_get_drvdata(vdev);
	int i;

	for (i = 0; i < mdp->mdp_data->pp_used; i++)
		if (mdp->cmdq_clt[i])
			cmdq_mbox_destroy(mdp->cmdq_clt[i]);

	scp_put(mdp->scp);

	destroy_workqueue(mdp->job_wq);
	destroy_workqueue(mdp->clock_wq);

	pm_runtime_disable(&mdp->pdev->dev);

	vb2_dma_contig_clear_max_seg_size(&mdp->pdev->dev);

	mdp_comp_destroy(mdp);
	for (i = 0; i < mdp->mdp_data->pipe_info_len; i++) {
		enum mdp_mm_subsys_id idx;
		struct mtk_mutex *m;
		u32 m_id;

		idx = mdp->mdp_data->pipe_info[i].sub_id;
		m_id = mdp->mdp_data->pipe_info[i].mutex_id;
		m = mdp->mm_subsys[idx].mdp_mutex[m_id];
		if (!IS_ERR_OR_NULL(m))
			mtk_mutex_put(m);
	}

	mdp_vpu_shared_mem_free(&mdp->vpu);
	v4l2_m2m_release(mdp->m2m_dev);
	kfree(mdp);
}

static int mdp_mm_subsys_deploy(struct mdp_dev *mdp, enum mdp_infra_id id)
{
	struct platform_device *mm_pdev = NULL;
	struct device **dev;
	int i;

	if (!mdp)
		return -EINVAL;

	for (i = 0; i < MDP_MM_SUBSYS_MAX; i++) {
		const char *compat;
		enum mdp_infra_id sub_id = id + i;

		switch (id) {
		case MDP_INFRA_MMSYS:
			dev = &mdp->mm_subsys[i].mmsys;
			break;
		case MDP_INFRA_MUTEX:
			dev = &mdp->mm_subsys[i].mutex;
			break;
		default:
			dev_err(&mdp->pdev->dev, "Unknown infra id %d", id);
			return -EINVAL;
		}

		/*
		 * Not every chip has multiple multimedia subsystems, so
		 * the config may be null.
		 */
		compat = mdp->mdp_data->mdp_probe_infra[sub_id].compatible;
		if (strlen(compat) == 0)
			continue;

		mm_pdev = __get_pdev_by_id(mdp->pdev, mm_pdev, sub_id);
		if (WARN_ON(!mm_pdev))
			return -ENODEV;

		*dev = &mm_pdev->dev;
	}

	return 0;
}

static int mdp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mdp_dev *mdp;
	struct platform_device *mm_pdev;
	struct resource *res;
	int ret, i, mutex_id;

	mdp = kzalloc(sizeof(*mdp), GFP_KERNEL);
	if (!mdp) {
		ret = -ENOMEM;
		goto err_return;
	}

	mdp->pdev = pdev;
	mdp->mdp_data = of_device_get_match_data(&pdev->dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res->start != mdp->mdp_data->mdp_con_res) {
		platform_set_drvdata(pdev, mdp);
		goto success_return;
	}

	ret = mdp_mm_subsys_deploy(mdp, MDP_INFRA_MMSYS);
	if (ret)
		goto err_destroy_device;

	ret = mdp_mm_subsys_deploy(mdp, MDP_INFRA_MUTEX);
	if (ret)
		goto err_destroy_device;

	for (i = 0; i < mdp->mdp_data->pipe_info_len; i++) {
		enum mdp_mm_subsys_id idx;
		struct mtk_mutex **m;

		idx = mdp->mdp_data->pipe_info[i].sub_id;
		mutex_id = mdp->mdp_data->pipe_info[i].mutex_id;
		m = &mdp->mm_subsys[idx].mdp_mutex[mutex_id];

		if (!IS_ERR_OR_NULL(*m))
			continue;

		*m = mtk_mutex_get(mdp->mm_subsys[idx].mutex);
		if (IS_ERR(*m)) {
			ret = PTR_ERR(*m);
			goto err_free_mutex;
		}
	}

	ret = mdp_comp_config(mdp);
	if (ret) {
		dev_err(dev, "Failed to config mdp components\n");
		goto err_free_mutex;
	}

	mdp->job_wq = alloc_workqueue(MDP_MODULE_NAME, WQ_FREEZABLE, 0);
	if (!mdp->job_wq) {
		dev_err(dev, "Unable to create job workqueue\n");
		ret = -ENOMEM;
		goto err_deinit_comp;
	}

	mdp->clock_wq = alloc_workqueue(MDP_MODULE_NAME "-clock", WQ_FREEZABLE,
					0);
	if (!mdp->clock_wq) {
		dev_err(dev, "Unable to create clock workqueue\n");
		ret = -ENOMEM;
		goto err_destroy_job_wq;
	}

	mdp->scp = scp_get(pdev);
	if (!mdp->scp) {
		mm_pdev = __get_pdev_by_id(pdev, NULL, MDP_INFRA_SCP);
		if (WARN_ON(!mm_pdev)) {
			dev_err(&pdev->dev, "Could not get scp device\n");
			ret = -ENODEV;
			goto err_destroy_clock_wq;
		}
		mdp->scp = platform_get_drvdata(mm_pdev);
	}

	mdp->rproc_handle = scp_get_rproc(mdp->scp);
	dev_dbg(&pdev->dev, "MDP rproc_handle: %pK", mdp->rproc_handle);

	mutex_init(&mdp->vpu_lock);
	mutex_init(&mdp->m2m_lock);

	for (i = 0; i < mdp->mdp_data->pp_used; i++) {
		mdp->cmdq_clt[i] = cmdq_mbox_create(dev, i);
		if (IS_ERR(mdp->cmdq_clt[i])) {
			ret = PTR_ERR(mdp->cmdq_clt[i]);
			goto err_mbox_destroy;
		}

		mdp->cmdq_shift_pa[i] = cmdq_get_shift_pa(mdp->cmdq_clt[i]->chan);
	}

	init_waitqueue_head(&mdp->callback_wq);
	ida_init(&mdp->mdp_ida);
	platform_set_drvdata(pdev, mdp);

	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	ret = v4l2_device_register(dev, &mdp->v4l2_dev);
	if (ret) {
		dev_err(dev, "Failed to register v4l2 device\n");
		ret = -EINVAL;
		goto err_mbox_destroy;
	}

	ret = mdp_m2m_device_register(mdp);
	if (ret) {
		v4l2_err(&mdp->v4l2_dev, "Failed to register m2m device\n");
		goto err_unregister_device;
	}

success_return:
	dev_dbg(dev, "mdp-%d registered successfully\n", pdev->id);
	return 0;

err_unregister_device:
	v4l2_device_unregister(&mdp->v4l2_dev);
err_mbox_destroy:
	while (--i >= 0)
		cmdq_mbox_destroy(mdp->cmdq_clt[i]);
	scp_put(mdp->scp);
err_destroy_clock_wq:
	destroy_workqueue(mdp->clock_wq);
err_destroy_job_wq:
	destroy_workqueue(mdp->job_wq);
err_deinit_comp:
	mdp_comp_destroy(mdp);
err_free_mutex:
	for (i = 0; i < mdp->mdp_data->pipe_info_len; i++) {
		enum mdp_mm_subsys_id idx;
		struct mtk_mutex *m;

		idx = mdp->mdp_data->pipe_info[i].sub_id;
		mutex_id = mdp->mdp_data->pipe_info[i].mutex_id;
		m = mdp->mm_subsys[idx].mdp_mutex[mutex_id];
		if (!IS_ERR_OR_NULL(m))
			mtk_mutex_put(m);
	}
err_destroy_device:
	kfree(mdp);
err_return:
	dev_dbg(dev, "Errno %d\n", ret);
	return ret;
}

static void mdp_remove(struct platform_device *pdev)
{
	struct mdp_dev *mdp = platform_get_drvdata(pdev);

	v4l2_device_unregister(&mdp->v4l2_dev);

	dev_dbg(&pdev->dev, "%s driver unloaded\n", pdev->name);
}

static int __maybe_unused mdp_suspend(struct device *dev)
{
	struct mdp_dev *mdp = dev_get_drvdata(dev);
	int ret;

	atomic_set(&mdp->suspended, 1);

	if (refcount_read(&mdp->job_count)) {
		ret = wait_event_timeout(mdp->callback_wq,
					 !refcount_read(&mdp->job_count),
					 2 * HZ);
		if (ret == 0) {
			dev_err(dev,
				"%s:flushed cmdq task incomplete, count=%d\n",
				__func__, refcount_read(&mdp->job_count));
			return -EBUSY;
		}
	}

	return 0;
}

static int __maybe_unused mdp_resume(struct device *dev)
{
	struct mdp_dev *mdp = dev_get_drvdata(dev);

	atomic_set(&mdp->suspended, 0);

	return 0;
}

static const struct dev_pm_ops mdp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mdp_suspend, mdp_resume)
};

static struct platform_driver mdp_driver = {
	.probe		= mdp_probe,
	.remove		= mdp_remove,
	.driver = {
		.name	= MDP_MODULE_NAME,
		.pm	= &mdp_pm_ops,
		.of_match_table = mdp_of_ids,
	},
};

module_platform_driver(mdp_driver);

MODULE_AUTHOR("Ping-Hsun Wu <ping-hsun.wu@mediatek.com>");
MODULE_DESCRIPTION("MediaTek image processor 3 driver");
MODULE_LICENSE("GPL");
