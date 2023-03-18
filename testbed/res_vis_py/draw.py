import matplotlib.pyplot as plt
import numpy as np
from data import *

AZURE_LRC_PARAMETERS = [
    (8, 4, 2), (12, 6, 3), (12, 8, 4), (15, 10, 5), (16, 10, 5), (16, 12, 6), (18, 12, 5), (24, 15, 5)
]

AZURE_LRC_1_PARAMETERS = [
    (10, 6, 3), (12, 6, 3), (15, 10, 5), (16, 10, 5), (17, 12, 6), (18, 12, 5), (24, 15, 5)
]

STR_AZURE_LRC_PARAMETERS = [
    "(8, 4, 2)", "(12, 6, 3)", "(12, 8, 4)", "(15, 10, 5)", "(16, 10, 5)", "(16, 12, 6)", "(18, 12, 5)", "(24, 15, 5)"
]

STR_AZURE_LRC_1_PARAMETERS = [
    "(10, 6, 3)", "(12, 6, 3)", "(15, 10, 5)", "(16, 10, 5)", "(17, 12, 6)", "(18, 12, 5)", "(24, 15, 5)"
]

OBJECT_SIZE_LABELS = ["256KB", "512KB", "1MB", "2MB", "4MB"]
BANDWIDTH_SIZE_LABELS = ["0.5", "0.75", "1", "2", "5", "10"]
X_LABELS = ["Coding Parameter", "Object Size", "Bandwidth (Gbps)"]
Y_LABELS = ["Degraded Read Cost (us)", "Node Repair Rate (MiB/s)", "Normalized Repair Cost (us)"]

EXP1_DRC = [[4848, 6564, 11448, 24840, 49244],
            [3960, 6262, 10712, 22525, 44102],
            [1628, 2439, 4329, 8272, 16764]]

datas = [[8.51992, 6.190264, 5.621135, 5.05101, 5.06524, 5.976286, 4.425562],
         [8.262278, 8.023622, 5.795086, 5.593467, 5.363196, 6.238459, 4.781715],
         [17.83294, 30.59227, 13.97819, 15.59089, 10.28807, 15.62305, 24.93144]]
datas_max = [[8.598157, 6.357441, 5.753078, 5.165289, 5.200533, 6.231927, 4.566836],
            [8.376893, 8.11662, 6.056935, 5.71037, 5.547419, 6.304534, 5.023863],
            [18.44066, 33.98586, 14.16832, 15.95914, 10.83377, 16.00102, 26.4131]]
datas_min = [[8.400538, 6.053562, 5.454347, 4.919323, 4.83821, 5.847953, 4.213542],
            [8.064776, 7.897397, 5.468665, 5.418292, 5.076967, 6.134668, 4.399859],
            [17.30343, 27.75003, 13.79691, 15.2207, 9.922998, 15.39219, 21.64502]]

def cal_err(datas, datas_max, datas_min):
    std_err = []
    for i in range(len(datas)):
        neg_err = []
        pos_err = []
        tmp_list = []
        for j in range(len(datas[i])):
            neg = datas[i][j] - datas_min[i][j]
            pos = datas_max[i][j] - datas[i][j]
            neg_err.append(neg)
            pos_err.append(pos)
        neg_err_t = tuple(neg_err)
        pos_err_t = tuple(pos_err)
        tmp_list.append(neg_err_t)
        tmp_list.append(pos_err_t)
        std_err.append(tmp_list)
    return std_err

def draw_multi_bars(datas, std_err, labels, ylabel):
    bar_width = 0.2
    col = ["#2878B5", "#9AC9DB", "#3CB371"]
    err_attr = {"elinewidth": 1, "ecolor": "black", "capsize": 2}
    bar_1 = np.arange(len(labels))
    bar_2 = [i+bar_width for i in bar_1]
    bar_3 = [i+bar_width for i in bar_2]
    plt.figure(figsize=(10, 6), dpi=80)
    plt.subplots_adjust(left=0.25, right=0.75, top=0.85, bottom=0.25)
    plt.bar(bar_1, datas[0], color=col[0], edgecolor='black', width=bar_width, label='flat', yerr=std_err[0], error_kw=err_attr)
    plt.bar(bar_2, datas[1], color=col[1], edgecolor='black', width=bar_width, label='random', yerr=std_err[1], error_kw=err_attr)
    plt.bar(bar_3, datas[2], color=col[2], edgecolor='black', width=bar_width, label='optimal', yerr=std_err[2], error_kw=err_attr)
    plt.xlabel("Coding parameters")
    plt.ylabel(ylabel=ylabel)
    plt.xticks(bar_2, labels, rotation=45)
    plt.legend()
    plt.show()

def draw_multi_plots(datas, labels, xlabel, ylabel):
    col = ["#2878B5", "#9AC9DB", "#3CB371"]
    x = np.arange((len(labels)))
    plt.figure(figsize=(10, 6), dpi=80)
    plt.subplots_adjust(left=0.25, right=0.75, top=0.85, bottom=0.25)
    plt.plot(x, datas[0], marker='s', color=col[0], label='flat', markersize=6)
    plt.plot(x, datas[1], marker='o', color=col[1], label='random', markersize=6)
    plt.plot(x, datas[2], marker='*', color=col[2], label='optimal', markersize=6)
    plt.xlabel(xlabel=xlabel)
    plt.ylabel(ylabel=ylabel)
    plt.xticks(x, labels)
    plt.legend()
    plt.show()

if __name__ == '__main__':
    # std_err = cal_err(datas, datas_max, datas_min)
    # draw_multi_bars(datas, std_err, STR_AZURE_LRC_1_PARAMETERS, Y_LABELS[1])

    #   Azure-LRC
    #   experiment 1
    draw_multi_plots(AZURE_LRC_EXP1_DRC, OBJECT_SIZE_LABELS, X_LABELS[1], Y_LABELS[0])
    draw_multi_plots(AZURE_LRC_EXP1_NRR, OBJECT_SIZE_LABELS, X_LABELS[1], Y_LABELS[1])
    #   experiment 2
    std_err = cal_err(AZURE_LRC_EXP2_DRC_512, AZURE_LRC_EXP2_DRC_MAX_512, AZURE_LRC_EXP2_DRC_MIN_512)
    draw_multi_bars(AZURE_LRC_EXP2_DRC_512, std_err, STR_AZURE_LRC_PARAMETERS, Y_LABELS[0])
    std_err = cal_err(AZURE_LRC_EXP2_NRR_512, AZURE_LRC_EXP2_NRR_MAX_512, AZURE_LRC_EXP2_NRR_MIN_512)
    draw_multi_bars(AZURE_LRC_EXP2_NRR_512, std_err, STR_AZURE_LRC_PARAMETERS, Y_LABELS[1])
    std_err = cal_err(AZURE_LRC_EXP2_DRC_1024, AZURE_LRC_EXP2_DRC_MAX_1024, AZURE_LRC_EXP2_DRC_MIN_1024)
    draw_multi_bars(AZURE_LRC_EXP2_DRC_1024, std_err, STR_AZURE_LRC_PARAMETERS, Y_LABELS[0])
    std_err = cal_err(AZURE_LRC_EXP2_NRR_1024, AZURE_LRC_EXP2_NRR_MAX_1024, AZURE_LRC_EXP2_NRR_MIN_1024)
    draw_multi_bars(AZURE_LRC_EXP2_NRR_1024, std_err, STR_AZURE_LRC_PARAMETERS, Y_LABELS[1])
    #   experiment 3
    draw_multi_plots(AZURE_LRC_EXP3_DRC, BANDWIDTH_SIZE_LABELS, X_LABELS[2], Y_LABELS[0])
    draw_multi_plots(AZURE_LRC_EXP3_NRR, BANDWIDTH_SIZE_LABELS, X_LABELS[2], Y_LABELS[1])

    #   Azure-LRC+1
    #   experiment 1
    draw_multi_plots(AZURE_LRC_1_EXP1_DRC, OBJECT_SIZE_LABELS, X_LABELS[1], Y_LABELS[0])
    draw_multi_plots(AZURE_LRC_1_EXP1_NRR, OBJECT_SIZE_LABELS, X_LABELS[1], Y_LABELS[1])
    #   experiment 2
    std_err = cal_err(AZURE_LRC_1_EXP2_DRC_512, AZURE_LRC_1_EXP2_DRC_MAX_512, AZURE_LRC_1_EXP2_DRC_MIN_512)
    draw_multi_bars(AZURE_LRC_1_EXP2_DRC_512, std_err, STR_AZURE_LRC_1_PARAMETERS, Y_LABELS[0])
    std_err = cal_err(AZURE_LRC_1_EXP2_NRR_512, AZURE_LRC_1_EXP2_NRR_MAX_512, AZURE_LRC_1_EXP2_NRR_MIN_512)
    draw_multi_bars(AZURE_LRC_1_EXP2_NRR_512, std_err, STR_AZURE_LRC_1_PARAMETERS, Y_LABELS[1])
    std_err = cal_err(AZURE_LRC_1_EXP2_DRC_1024, AZURE_LRC_1_EXP2_DRC_MAX_1024, AZURE_LRC_1_EXP2_DRC_MIN_1024)
    draw_multi_bars(AZURE_LRC_1_EXP2_DRC_1024, std_err, STR_AZURE_LRC_1_PARAMETERS, Y_LABELS[0])
    std_err = cal_err(AZURE_LRC_1_EXP2_NRR_1024, AZURE_LRC_1_EXP2_NRR_MAX_1024, AZURE_LRC_1_EXP2_NRR_MIN_1024)
    draw_multi_bars(AZURE_LRC_1_EXP2_NRR_1024, std_err, STR_AZURE_LRC_1_PARAMETERS, Y_LABELS[1])
    #   experiment 3
    draw_multi_plots(AZURE_LRC_1_EXP3_DRC, BANDWIDTH_SIZE_LABELS, X_LABELS[2], Y_LABELS[0])
    draw_multi_plots(AZURE_LRC_1_EXP3_NRR, BANDWIDTH_SIZE_LABELS, X_LABELS[2], Y_LABELS[1])


