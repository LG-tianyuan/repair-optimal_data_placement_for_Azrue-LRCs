import math
from utils import Code_Placement
from utils import cluster

class Azure_LRC_1(Code_Placement):
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
        for group in self.stripe_information:
            for item in group:
                repair_request = [i for i in group if not i == item]
                self.block_repair_request[item] = repair_request

    def return_group_number(self):
        pass

    def generate_best_placement(self):
        cluster_id = 0
        self.b = self.d - 1
        for group in self.stripe_information:
            if self.r + 1 <= self.b:
                new_cluster = cluster(cluster_id, self.b)
                for block in group:
                    new_cluster.add_new_block(block, self.block_to_groupnumber[block])
                    self.best_placement['block_map_clusternumber'][block] = cluster_id
                self.best_placement['raw_information'].append(new_cluster)
                cluster_id += 1
            else:
                for block_id in range(0, len(group), self.b):
                    new_cluster = cluster(cluster_id, self.b)
                    for block in group[block_id:block_id+self.b]:
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
        l = math.ceil(k / r) + 1
        g = n - k - l
        return k, l, g, r

    def klgr_to_nkr(self, k, l, g, r):
        n = k + l + g
        return n, k, r

    def check_parameter(self):
        assert self.n > self.k + self.l, 'Parameters do not meet requirements!'

if __name__ == '__main__':
    azure_lrc_1 = Azure_LRC_1()
    select = 1
    if select == 0:
        n = 20
        k = 14
        r = 5
        k, l, g, r = azure_lrc_1.nkr_to_klgr(n, k, r)
        print('Azure-LRC+1(k,l,g,r)', k, l, g, r)
        # print(azure_lrc_1.return_DRC_NRC(k, l, g, r, "flat", 10, True))
        # print(azure_lrc_1.return_DRC_NRC(k, l, g, r, "random", 10, True))
        print(azure_lrc_1.return_DRC_NRC(k, l, g, r, "best", 10, True))
    else:
        test_case = [
            [12, 8, 4], [12, 8, 5], [14, 9, 3], [14, 9, 4], [14, 10, 5],
            [14, 10, 6], [15, 10, 4], [15, 10, 5], [16, 10, 5], [16, 10, 6],
            [16, 11, 5], [16, 11, 6], [16, 12, 6], [16, 12, 7], [17, 12, 4],
            [17, 12, 5], [17, 12, 6], [18, 12, 3], [18, 12, 4], [18, 12, 5]
        ]
        test_case = [
            [17, 8, 2], [15, 9, 3], [18, 12, 4], [14, 8, 3], [16, 10, 4], [21, 15, 6],
            [17, 11, 4], [18, 11, 5], [16, 9, 4], [15, 9, 4], [19, 13, 5], [20, 14, 5]
        ]
        for i in range(len(test_case)):
            n = test_case[i][0]
            k = test_case[i][1]
            r = test_case[i][2]
            k, l, g, r = azure_lrc_1.nkr_to_klgr(n, k, r)
            print('Azure-LRC+1(k,l,g,r)', k, l, g, r)
            print(azure_lrc_1.return_DRC_NRC(k, l, g, r, "best", 10, False))