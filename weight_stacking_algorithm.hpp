// 수정중

bool has_support(const std::tuple<int, int, int, int, int, int>& new_box, 
                const std::vector<std::tuple<int, int, int, int, int, int>>& placements) {
    int x, y, z, width, length, height;
    std::tie(x, y, z, width, length, height) = new_box;
    
    if (z == 0) return true;  // 바닥에 있으면 지지됨
    
    // 박스 바닥면적의 일정 비율(예: 30%)이 지지되어야 함
    int supported_area = 0;
    const int total_area = width * length;
    const double min_support_ratio = 0.3;  // 30% 이상 지지 필요
    
    for (const auto& existing : placements) {
        int ex, ey, ez, ew, el, eh;
        std::tie(ex, ey, ez, ew, el, eh) = existing;
        
        // 현재 박스 바로 아래에 있는 박스만 검사
        if (ez + eh == z) {  // 높이가 정확히 맞아야 함
            // 겹치는 영역 계산
            int overlap_x = std::max(0, 
                std::min(x + width, ex + ew) - std::max(x, ex));
            int overlap_y = std::max(0, 
                std::min(y + length, ey + el) - std::max(y, ey));
            
            supported_area += overlap_x * overlap_y;
        }
    }
    
    return (static_cast<double>(supported_area) / total_area) >= min_support_ratio;
}