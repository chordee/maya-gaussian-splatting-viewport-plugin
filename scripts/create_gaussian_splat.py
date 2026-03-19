# scripts/create_gaussian_splat.py
import maya.cmds as cmds

def create_gaussian_splat(path="D:/tmp/PLY/a-room-179.ply"):
    # Create the node
    node = cmds.createNode("gaussianSplatNode")
    
    # Set the file path
    cmds.setAttr(f"{node}.filePath", path, type="string")
    
    # Select the node and frame it
    cmds.select(node)
    cmds.viewFit()
    
    print(f"[GaussianSplat] Created node: {node} with path: {path}")
    return node

if __name__ == "__main__":
    create_gaussian_splat()
