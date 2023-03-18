from parameters import PARAMETERS_DIFF_PLACEMENT,STR_PARAMETERS_DIFF_PLACEMENT
from draw_bar import create_multi_bars1
from Azure_LRC_1_v2 import Azure_LRC_1
from Azure_LRC import Azure_LRC

def gen_data_diff_placement_Azure_LRC():
    flat_DRC_list = []
    flat_NRC_list = []
    random_DRC_list = []
    random_NRC_list = []
    opt_DRC_list = []
    opt_NRC_list = []
    opt_list = []
    azure_lrc = Azure_LRC()
    for i in range(len(PARAMETERS_DIFF_PLACEMENT)):
        n = PARAMETERS_DIFF_PLACEMENT[i][0]
        k = PARAMETERS_DIFF_PLACEMENT[i][1]
        r = PARAMETERS_DIFF_PLACEMENT[i][2]
        k, l, g, r = azure_lrc.nkr_to_klgr(n, k, r)
        flat_DRC, flat_NRC = azure_lrc.return_DRC_NRC(k, l, g, r, "flat", 10, False)
        random_DRC, random_NRC = azure_lrc.return_DRC_NRC(k, l, g, r, "random", 10, False)
        opt_DRC, opt_NRC = azure_lrc.return_DRC_NRC(k, l, g, r, "best", 10, False)
        flat_DRC_list.append(flat_DRC)
        flat_NRC_list.append(flat_NRC)
        random_DRC_list.append(random_DRC)
        random_NRC_list.append(random_NRC)
        opt_DRC_list.append(opt_DRC)
        opt_NRC_list.append(opt_NRC)
        opt_list.append(opt_NRC * n / k)
    DRC = [flat_DRC_list, random_DRC_list, opt_DRC_list]
    NRC = [flat_NRC_list, random_NRC_list, opt_NRC_list]
    return DRC, NRC, opt_list

def gen_data_diff_placement_Azure_LRC_1():
    flat_DRC_list = []
    flat_NRC_list = []
    random_DRC_list = []
    random_NRC_list = []
    opt_DRC_list = []
    opt_NRC_list = []
    opt_list = []
    azure_lrc_1 = Azure_LRC_1()
    for i in range(len(PARAMETERS_DIFF_PLACEMENT)):
        n = PARAMETERS_DIFF_PLACEMENT[i][0]
        k = PARAMETERS_DIFF_PLACEMENT[i][1]
        r = PARAMETERS_DIFF_PLACEMENT[i][2]
        k, l, g, r = azure_lrc_1.nkr_to_klgr(n, k, r)
        flat_DRC, flat_NRC = azure_lrc_1.return_DRC_NRC(k, l, g, r, "flat", 10, False)
        random_DRC, random_NRC = azure_lrc_1.return_DRC_NRC(k, l, g, r, "random", 10, False)
        opt_DRC, opt_NRC = azure_lrc_1.return_DRC_NRC(k, l, g, r, "best", 10, False)
        flat_DRC_list.append(flat_DRC)
        flat_NRC_list.append(flat_NRC)
        random_DRC_list.append(random_DRC)
        random_NRC_list.append(random_NRC)
        opt_DRC_list.append(opt_DRC)
        opt_NRC_list.append(opt_NRC)
        opt_list.append(opt_NRC * n / k)
    DRC = [flat_DRC_list, random_DRC_list, opt_DRC_list]
    NRC = [flat_NRC_list, random_NRC_list, opt_NRC_list]
    return DRC, NRC, opt_list

if __name__ == '__main__':
    my_label = ["Flat NRC","Random NRC","Opt NRC","DRC"]
    DRC, NRC, opt = gen_data_diff_placement_Azure_LRC()
    print(opt)
    yticks = [2, 4, 6, 8, 10, 12]
    create_multi_bars1(yticks, my_label, STR_PARAMETERS_DIFF_PLACEMENT, NRC, DRC, tick_step=10, group_gap=2, bar_gap=0, nu_list_str=STR_PARAMETERS_DIFF_PLACEMENT)
