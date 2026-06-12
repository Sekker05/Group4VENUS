from math import hypot
import heapq
from collections import deque

class venusMap:
    def __init__(self, gridSize=10):
        self.gridSize = gridSize
        self.known = {}
        self.obstacles = set()
        self.clankers = []
        self.bingo = []
        self.pending_cubes = []
        self.pending_mountains = [] 
        
        self._unknown_cache = None   
        self._known_snapshot = None  

    def gridLock(self, pos):
        """Snap coordinate to grid."""
        return (int(round(pos[0]/self.gridSize)*self.gridSize),
                int(round(pos[1]/self.gridSize)*self.gridSize))
    
    def tick_pending(self):
        """Process cooldowns."""
        for p in self.pending_cubes:
            p[2] = max(0, p[2] - 1)
        for p in self.pending_mountains:
            p[2] = max(0, p[2] - 1)
    
    def claim(self, pos):
        """Mark tile as explored."""
        policko = self.gridLock(pos)
        self.known[policko] = self.known.get(policko, 0) + 1 

    def mark_obstacle(self, pos):
        self.obstacles.add(self.gridLock(pos))

    def found_cubes(self, pos):
        """Register cube if not already known."""
        if any(hypot(pos[0]-c[0], pos[1]-c[1]) < 10 for c in self.bingo):
            return
        self.bingo.append(pos)
        self.pending_cubes = [p for p in self.pending_cubes if hypot(p[0]-pos[0], p[1]-pos[1]) > 15]
        print(f'I got a jar of dirt @{pos} count: {len(self.bingo)}') 
    
    def feel_the_heat(self, pos):
        return self.known.get(self.gridLock(pos), 0)

    def find_nearest_unknown(self, start_pos):
        """BFS to find closest unmapped tile."""
        current_snapshot = frozenset(self.known.keys())
        
        # Use cache if map didn't change
        if current_snapshot == self._known_snapshot and self._unknown_cache is not None:
            return self._unknown_cache

        self._known_snapshot = current_snapshot
        start_node = self.gridLock(start_pos)
        
        # Fast queue for BFS search
        queue = deque([start_node])
        visited = {start_node}
        
        iters = 0
        while queue:
            iters += 1
            if iters > 5000: break # Hard safety cutoff
                
            curr = queue.popleft()
            
            # Found unmapped tile
            if curr not in self.known:
                self._unknown_cache = curr
                return curr
                
            # Expand neighbors
            for dx, dy in [(self.gridSize, 0), (-self.gridSize, 0), (0, self.gridSize), (0, -self.gridSize)]:
                nxt = (curr[0] + dx, curr[1] + dy)
                if nxt not in visited and nxt not in self.obstacles:
                    visited.add(nxt)
                    queue.append(nxt)

        self._unknown_cache = None
        return None

    def astar(self, start_pos, goal_pos):
        """A* pathfinding."""
        start = self.gridLock(start_pos)
        goal = self.gridLock(goal_pos)
        
        open_set = []
        heapq.heappush(open_set, (0, start))
        came_from = {}
        g_score = {start: 0}
        
        # 8-way movement
        moves = [
            (self.gridSize, 0), (-self.gridSize, 0), (0, self.gridSize), (0, -self.gridSize),
            (self.gridSize, self.gridSize), (-self.gridSize, -self.gridSize), 
            (self.gridSize, -self.gridSize), (-self.gridSize, self.gridSize)
        ]
        
        iters = 0
        while open_set:
            iters += 1
            if iters > 2000: break # Hard safety cutoff
            
            _, current = heapq.heappop(open_set)
            
            # Target reached
            if current == goal:
                path = []
                while current in came_from:
                    path.append(current)
                    current = came_from[current]
                return path[::-1] # Reverse path
            
            # Evaluate neighbors
            for dx, dy in moves:
                nxt = (current[0] + dx, current[1] + dy)
                
                # Skip obstacles
                if nxt in self.obstacles: continue
                cost = g_score[current] + hypot(dx, dy)
                
                # Penalty for hugging walls
                for ox, oy in [(-self.gridSize, 0), (self.gridSize, 0), (0, -self.gridSize), (0, self.gridSize)]:
                    if (nxt[0]+ox, nxt[1]+oy) in self.obstacles:
                        cost += 100 

                # Attract to uncollected cubes
                if self.pending_cubes:
                    for cx, cy, _ in self.pending_cubes:
                        if hypot(nxt[0]-cx, nxt[1]-cy) < 30: cost -= 40    

                # Better path found
                if nxt not in g_score or cost < g_score[nxt]:
                    came_from[nxt] = current
                    g_score[nxt] = cost
                    f_score = cost + hypot(goal[0]-nxt[0], goal[1]-nxt[1])
                    heapq.heappush(open_set, (f_score, nxt))

        return []