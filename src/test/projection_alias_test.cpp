#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "optimizer/planner.h"

int main() {
    std::vector<TabCol> source_columns{{"fixture", "amount"}};
    std::vector<std::string> output_names{"amount_alias"};
    ProjectionPlan plan(T_Projection, std::shared_ptr<Plan>(),
                        source_columns, false, -1, output_names);

    if (plan.sel_cols_.size() != 1 ||
        plan.sel_cols_[0].col_name != "amount") {
        std::cerr << "projection source column changed while applying alias\n";
        return EXIT_FAILURE;
    }
    if (plan.output_names_.size() != 1 ||
        plan.output_names_[0] != "amount_alias") {
        std::cerr << "projection did not preserve SELECT AS output name\n";
        return EXIT_FAILURE;
    }
    std::cout << "projection alias test passed\n";
    return EXIT_SUCCESS;
}
