import math
from utils import Code_Placement
from utils import cluster

class Azure_LRC(Code_Placement):
    def generate_stripe_information(self):
        group_ptr = 0
        self.stripe_information.append([])
        for i in range(self.k):
            if i == self.r * (group_ptr + 1):
                group_ptr += 1
                self.stripe_information.append([])
            block = self.index_to_str('D', i)
            self.stripe_information[group_ptr].append(block)
            self.block_to_groupnumber[block] = group_ptr
        group_ptr += 1
        self.stripe_information.append([])
        for i in range(self.g):
            block = self.index_to_str('G', i)
            self.stripe_information[group_ptr].append(block)
            self.block_to_groupnumber[block] = group_ptr
        for i in range(self.l):
            block = self.index_to_str('L', i)
            self.stripe_information[i].append(block)
            self.block_to_groupnumber[block] = i

    def generate_block_repair_request(self):
        for group in self.stripe_information[:-1]:
            for item in group:
                repair_request = [i for i in group if not i == item]
                self.block_repair_request[item] = repair_request
        for item in self.stripe_information[-1]:
            repair_request = self.raw_stripe[:self.k]
            self.block_repair_request[item] = repair_request

    def return_group_number(self):
        pass

    def generate_best_placement(self):
        cluster_id = 0
        self.b = self.d - 1
        theta = math.floor(self.g / self.r)
        remain_local_groups = []
        ii = 0
        for i in range(self.l):
            if self.r + 1 <= self.b:
                new_cluster = cluster(cluster_id, self.g + theta)
                for j in range(ii, ii + theta):
                    if j >= self.l:
                        break
                    group = self.stripe_information[j]
                    for block in group:
                        new_cluster.add_new_block(block, self.block_to_groupnumber[block])
                        self.best_placement['block_map_clusternumber'][block] = cluster_id
                self.best_placement['raw_information'].append(new_cluster)
                cluster_id += 1
                ii += theta
                if ii >= self.l:
                    break
            else:
                group = self.stripe_information[i]
                for block_id in range(0, len(group), self.b):
                    if block_id + self.b > len(group):
                        remain_local_groups.append(group[block_id:])
                    else:
                        new_cluster = cluster(cluster_id, self.b)
                        for block in group[block_id:block_id+self.b]:
                            new_cluster.add_new_block(block, self.block_to_groupnumber[block])
                            self.best_placement['block_map_clusternumber'][block] = cluster_id
                        self.best_placement['raw_information'].append(new_cluster)
                        cluster_id += 1
        new_cluster = cluster(cluster_id, self.b)
        global_group = self.stripe_information[self.l]
        for block in global_group:
            new_cluster.add_new_block(block, self.block_to_groupnumber[block])
            self.best_placement['block_map_clusternumber'][block] = cluster_id
        self.best_placement['raw_information'].append(new_cluster)
        g_cluster_id = cluster_id
        cluster_id += 1
        # if there is any tail, try to merge most tails into the least clusters
        # for Azure-LRC, sometimes it can help reduce the repair cost of global parity blocks
        # for example, (n, l, r) = (15, 10, 5)
        l_ = len(remain_local_groups)
        if l_ > 0:
            theta = -1
            if self.k % self.r == 0:
                m = len(remain_local_groups[0])
                if m == 1:  # (14, 9, 3)
                    for i in range(l_):
                        self.best_placement['raw_information'][g_cluster_id].set_upperbound(self.g + l_)
                        for block in remain_local_groups[i]:
                            self.best_placement['raw_information'][g_cluster_id].add_new_block(block,
                                                                                                 self.block_to_groupnumber[
                                                                                                     block])
                            self.best_placement['block_map_clusternumber'][block] = g_cluster_id
                else:  # (17, 12, 4)
                    theta = math.floor(self.g / (m - 1))
                    for i in range(0, l_, theta):
                        new_cluster = cluster(cluster_id, self.g + theta)
                        for j in range(i, i + theta):
                            if j >= l_:
                                break
                            for block in remain_local_groups[j]:
                                new_cluster.add_new_block(block, self.block_to_groupnumber[block])
                                self.best_placement['block_map_clusternumber'][block] = cluster_id
                        self.best_placement['raw_information'].append(new_cluster)
                        cluster_id += 1
            else:
                if (self.r + 1) % self.b != 0:
                    m = len(remain_local_groups[0])
                    if m == 1:  # (13, 8, 3)
                        for i in range(self.l - 1):
                            self.best_placement['raw_information'][g_cluster_id].set_upperbound(self.g + self.l - 1)
                            for block in remain_local_groups[i]:
                                self.best_placement['raw_information'][g_cluster_id].add_new_block(block,
                                                                                                     self.block_to_groupnumber[
                                                                                                         block])
                                self.best_placement['block_map_clusternumber'][block] = g_cluster_id
                    else:   # (15, 10, 4)
                        theta = math.floor(self.g / (m - 1))
                        for i in range(0, self.l - 1, theta):
                            new_cluster = cluster(cluster_id, self.g + theta)
                            for j in range(i, i + theta):
                                if j >= self.l - 1:
                                    break
                                for block in remain_local_groups[j]:
                                    new_cluster.add_new_block(block, self.block_to_groupnumber[block])
                                    self.best_placement['block_map_clusternumber'][block] = cluster_id
                            self.best_placement['raw_information'].append(new_cluster)
                            cluster_id += 1
                    if self.l == l_:
                        m_ = len(remain_local_groups[-1])
                        index = math.floor((self.l - 1) / theta) * theta
                        if m_ == 1:
                            if m == 1:  # (20, 15, 6)
                                self.best_placement['raw_information'][g_cluster_id].set_upperbound(self.g + self.l)
                            else:   # (16, 11, 4)
                                self.best_placement['raw_information'][g_cluster_id].set_upperbound(self.g + 1)
                            for block in remain_local_groups[-1]:
                                self.best_placement['raw_information'][g_cluster_id].add_new_block(block,
                                                                                                   self.block_to_groupnumber[
                                                                                                       block])
                                self.best_placement['block_map_clusternumber'][block] = g_cluster_id
                        elif m != 1 and self.l - 1 != index and (l_ - 1 - index) * m + m_ <= self.g + (l_ - index):  # (17,11,5)
                            self.best_placement['raw_information'][cluster_id - 1].set_upperbound(self.g + l_ - index)
                            for block in remain_local_groups[-1]:
                                self.best_placement['raw_information'][cluster_id - 1].add_new_block(block, self.block_to_groupnumber[block])
                                self.best_placement['block_map_clusternumber'][block] = cluster_id - 1
                        else:  # (15, 9, 4), (14, 9, 4)
                            new_cluster = cluster(cluster_id, self.b)
                            for block in remain_local_groups[-1]:
                                new_cluster.add_new_block(block, self.block_to_groupnumber[block])
                                self.best_placement['block_map_clusternumber'][block] = cluster_id
                            self.best_placement['raw_information'].append(new_cluster)
                            cluster_id += 1
                else:
                    m_ = len(remain_local_groups[-1])
                    if m_ == 1:  # (18, 13, 5)
                        self.best_placement['raw_information'][g_cluster_id].set_upperbound(self.g + 1)
                        for block in remain_local_groups[-1]:
                            self.best_placement['raw_information'][g_cluster_id].add_new_block(block,
                                                                                               self.block_to_groupnumber[
                                                                                                   block])
                            self.best_placement['block_map_clusternumber'][block] = g_cluster_id
                    else:  # (19, 14, 5)
                        new_cluster = cluster(cluster_id, self.b)
                        for block in remain_local_groups[-1]:
                            new_cluster.add_new_block(block, self.block_to_groupnumber[block])
                            self.best_placement['block_map_clusternumber'][block] = cluster_id
                        self.best_placement['raw_information'].append(new_cluster)
                        cluster_id += 1
        print('total number of clusters:', cluster_id)
        assert self.check_cluster_information(self.best_placement)

    def calculate_distance(self):
        self.d = self.g + 2
        return self.d

    def nkr_to_klgr(self, n, k, r):
        l = math.ceil(k / r)
        g = n - k - l
        return k, l, g, r

    def klgr_to_nkr(self, k, l, g, r):
        n = k + l + g
        return n, k, r

    def check_parameter(self):
        assert self.n >= self.k + self.l, 'Parameters do not meet requirements!'

if __name__ == '__main__':
    azure_lrc = Azure_LRC()
    select = 1
    if select == 0:
        n = 14
        k = 10
        r = 5
        k, l, g, r = azure_lrc.nkr_to_klgr(n, k, r)
        print('Azure-LRC(k,l,g,r)', k, l, g, r)
        # print(azure_lrc.return_DRC_NRC(k, l, g, r, "flat", 10, True))
        # print(azure_lrc.return_DRC_NRC(k, l, g, r, "random", 10, True))
        print(azure_lrc.return_DRC_NRC(k, l, g, r, "best", 10, True))
    else:
        test_case = [
            [12, 8, 3], [12, 8, 4], [12, 8, 5], [14, 9, 3], [14, 9, 4], [14, 10, 4], [14, 10, 5],
            [14, 10, 6], [15, 10, 3], [15, 10, 4], [15, 10, 5], [16, 10, 5], [16, 10, 6], [16, 11, 3],
            [16, 11, 5], [16, 11, 6], [16, 12, 5], [16, 12, 6], [16, 12, 7], [17, 12, 4], [17, 12, 5],
            [18, 12, 3], [18, 12, 4], [18, 12, 5]
        ]
        test_case = [
            [16, 8, 2], [14, 9, 3], [17, 12, 4], [13, 8, 3], [15, 10, 4], [20, 15, 6],
            [16, 11, 4], [17, 11, 5], [15, 9, 4], [14, 9, 4], [18, 13, 5], [19, 14, 5]
        ]
        for i in range(len(test_case)):
            n = test_case[i][0]
            k = test_case[i][1]
            r = test_case[i][2]
            k, l, g, r = azure_lrc.nkr_to_klgr(n, k, r)
            print('Azure-LRC(k,l,g,r)', k, l, g, r)
            # print(azure_lrc.return_DRC_NRC(k, l, g, r, "flat", 10, False))
            # print(azure_lrc.return_DRC_NRC(k, l, g, r, "random", 10, False))
            print(azure_lrc.return_DRC_NRC(k, l, g, r, "best", 10, False))